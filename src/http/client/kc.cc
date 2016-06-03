#include <assert.h>
#include <string.h>
#include <errno.h>

#include "keystone-client.h"

/* The MIME type representing JavaScript Object Notation */
#define MIME_TYPE_JSON "application/json"
/* The content-type of the authentication requests we send */
/* Each of XML and JSON is allowed; we use JSON for brevity and simplicity */
#define KEYSTONE_AUTH_REQUEST_FORMAT MIME_TYPE_JSON
/* The content-type we desire to receive in authentication responses */
#define KEYSTONE_AUTH_RESPONSE_FORMAT MIME_TYPE_JSON
/* The portion of a JSON-encoded Keystone credentials POST body preceding the username */
#define KEYSTONE_AUTH_PAYLOAD_BEFORE_USERNAME "\
{\n\
	\"auth\":{\n\
		\"passwordCredentials\":{\n\
			\"username\":\""
/* The portion of a JSON-encoded Keystone credentials POST body succeeding the username and preceding the password */
#define KEYSTONE_AUTH_PAYLOAD_BEFORE_PASSWORD "\",\n\
			\"password\":\""
/* The portion of a JSON-encoded Keystone credentials POST body succeeding the password and preceding the tenant name */
#define KEYSTONE_AUTH_PAYLOAD_BEFORE_TENANT "\"\n\
		},\n\
		\"tenantName\":\""
/* The portion of a JSON-encoded Keystone credentials POST body succeeding the tenant name */
#define KEYSTONE_AUTH_PAYLOAD_END "\"\n\
	}\n\
}"
/* Number of elements in a statically-sized array */
#define ELEMENTSOF(arr) (sizeof(arr) / sizeof((arr)[0]))

/**
 *  Service type names in Keystone's catalog of services.
 *  Order must match that in enum openstack_service.
 */
static const char *const openstack_service_names[] = {
	"identity",     /* Keystone */
	"compute",      /* Nova */
	"ec2",          /* Nova EC2 */
	"object-store", /* Swift */
	"s3",           /* Swift S3 */
	"volume",       /* Cinder */
	"image"         /* Glance */
};

/**
 * Service endpoint URL type names, as seen in JSON object keys.
 * Order must match that in enum openstack_service_endpoint_type.
 */
static const char *const openstack_service_endpoint_url_type_names[] = {
	"publicURL",
	"adminURL",
	"internalURL"
};

/* HUman-friendly names for service endpoint URL types */
static const char *const openstack_service_endpoint_url_type_friendly_names[] = {
	"public",
	"admin",
	"internal"
};

/**
 * Default handler for libcurl errors.
 */
static void
default_curl_error_callback(const char *curl_funcname, CURLcode curl_err)
{
	assert(curl_funcname != NULL);
	fprintf(stderr, "%s failed: libcurl error code %ld: %s\n", curl_funcname, (long) curl_err, curl_easy_strerror(curl_err));
}

/**
 * Default handler for libjson errors.
 */
static void
default_json_error_callback(const char *json_funcname, enum json_tokener_error json_err)
{
	assert(json_funcname != NULL);
	assert(json_err != json_tokener_success);
	assert(json_err != json_tokener_continue);
	fprintf(stderr, "%s failed: libjson error %ld: %s\n", json_funcname, (long) json_err, json_tokener_error_desc(json_err));
}

/**
 * Default handler for Keystone errors.
 */
static void
default_keystone_error_callback(const char *keystone_operation, enum keystone_error keystone_err)
{
	assert(keystone_operation != NULL);
	assert(keystone_err != KSERR_SUCCESS);
	fprintf(stderr, "Keystone: %s: error %ld\n", keystone_operation, (long) keystone_err);
}

/**
 * Default memory [re-/de-]allocator.
 */
static void *
default_allocator(void *ptr, size_t size)
{
	if (0 == size) {
		if (ptr != NULL) {
			free(ptr);
		}
		return NULL;
	}
	if (NULL == ptr) {
		return malloc(size);
	}
	return realloc(ptr, size);
}

/**
 * To be called at start of user program, while still single-threaded.
 * Non-thread-safe and non-re-entrant.
 */
enum keystone_error
keystone_global_init(void)
{
	CURLcode curl_err;

	curl_err = curl_global_init(CURL_GLOBAL_ALL);
	if (curl_err != 0) {
		/* TODO: Output error indications about detected error in 'res' */
		return KSERR_INIT_FAILED;
	}

	return KSERR_SUCCESS;
}

/**
 * To be called at end of user program, while again single-threaded.
 * Non-thread-safe and non-re-entrant.
 */
void
keystone_global_cleanup(void)
{
	curl_global_cleanup();
}

/**
 * To be called by each thread of user program that will use this library,
 * before first other use of this library.
 * Thread-safe and re-entrant.
 */
enum keystone_error
keystone_start(keystone_context_t *context)
{
	assert(context != NULL);
	if (!context->curl_error) {
		context->curl_error = default_curl_error_callback;
	}
	if (!context->json_error) {
		context->json_error = default_json_error_callback;
	}
	if (!context->keystone_error) {
		context->keystone_error = default_keystone_error_callback;
	}
	if (!context->allocator) {
		context->allocator = default_allocator;
	}
	context->pvt.curl = curl_easy_init();
	if (NULL == context->pvt.curl) {
		/* NOTE: No error code from libcurl, so we assume/invent CURLE_FAILED_INIT */
		context->curl_error("curl_easy_init", CURLE_FAILED_INIT);
		return KSERR_INIT_FAILED;
	}

	return KSERR_SUCCESS;
}

/**
 * To be called by each thread of user program that will use this library,
 * after last other use of this library.
 * To be called once per successful call to keystone_start by that thread.
 * Thread-safe and re-entrant.
 */
void
keystone_end(keystone_context_t *context)
{
	assert(context != NULL);
	assert(context->pvt.curl != NULL);

	curl_easy_cleanup(context->pvt.curl);
	context->pvt.curl = NULL;
	if (context->pvt.auth_token != NULL) {
		context->pvt.auth_token = (char *)context->allocator(context->pvt.auth_token, 0);
	}
	if (context->pvt.auth_payload != NULL) {
		context->pvt.auth_payload = (char *)context->allocator(context->pvt.auth_payload, 0);
	}
	if (context->pvt.json_tokeniser != NULL) {
		json_tokener_free(context->pvt.json_tokeniser);
		context->pvt.json_tokeniser = NULL;
	}
}

/**
 * Control whether a proxy (eg HTTP or SOCKS) is used to access the Keystone server.
 * Argument must be a URL, or NULL if no proxy is to be used.
 */
enum keystone_error
keystone_set_proxy(keystone_context_t *context, const char *proxy_url)
{
	CURLcode curl_err;

	assert(context != NULL);
	assert(context->pvt.curl != NULL);

	curl_err = curl_easy_setopt(context->pvt.curl, CURLOPT_PROXY, (NULL == proxy_url) ? "" : proxy_url);
	if (CURLE_OK != curl_err) {
		context->curl_error("curl_easy_setopt", curl_err);
		return KSERR_INVARG;
	}

	return KSERR_SUCCESS;
}

/**
 * Control verbose logging to stderr of the actions of this library and the libraries it uses.
 * Currently this enables logging to standard error of libcurl's actions.
 */
enum keystone_error
keystone_set_debug(keystone_context_t *context, unsigned int enable_debugging)
{
	CURLcode curl_err;

	assert(context != NULL);
	assert(context->pvt.curl != NULL);

	context->pvt.debug = enable_debugging;

	curl_err = curl_easy_setopt(context->pvt.curl, CURLOPT_VERBOSE, enable_debugging ? 1 : 0);
	if (CURLE_OK != curl_err) {
		context->curl_error("curl_easy_setopt", curl_err);
		return KSERR_INVARG;
	}

	return KSERR_SUCCESS;
}

/**
 * Return the length of the given JSON array.
 */
static unsigned int
json_array_length(keystone_context_t *context, struct json_object *array)
{
	int len;

	assert(context != NULL);
	assert(array != NULL);

	if (json_object_is_type(array, json_type_array)) {
		len = json_object_array_length(array);
		if (len < 0) {
			context->keystone_error("JSON array length is negative", KSERR_PARSE);
			len = 0;
		} else if (len > (int)UINT_MAX) {
			context->keystone_error("JSON array length is too large for unsigned int", KSERR_PARSE);
			len = 0;
		}
	} else {
		context->keystone_error("JSON object is not an array", KSERR_PARSE);
		len = 0;
	}

	return (unsigned int) len;
}

/**
 * Return the index'th element of the given JSON array.
 */
static struct json_object *
json_array_get(keystone_context_t *context, struct json_object *array, unsigned int index)
{
	struct json_object *item;

	assert(context != NULL);
	assert(array != NULL);

	if (json_object_is_type(array, json_type_array)) {
		if (index < json_array_length(context, array)) {
			item = json_object_array_get_idx(array, index);
			if (NULL == item) {
				context->keystone_error("failed to index into JSON array", KSERR_PARSE);
			}
		} else {
			context->keystone_error("JSON array index out of bound", KSERR_PARSE);
			item = NULL;
		}
	} else {
		context->keystone_error("JSON object is not an array", KSERR_PARSE);
		item = NULL;
	}
	return item;
}

/**
 * Types of return from an enumerator function for a JSON array.
 */
enum item_callback_return {
	CONTINUE = 0, /* Continue iterating */
	STOP     = 1  /* Cease iteration */
};

typedef enum item_callback_return item_callback_return_t;

/**
 * A function which receives items from a JSON array.
 */
typedef item_callback_return_t (*item_callback_func_t)(keystone_context_t *context, struct json_object *item, void *callback_arg);

/**
 * Iterate over the given JSON array, passing elements of it to the given iteration function.
 * If the iteration function ever returns STOP, return the array element passed to it which caused it to first return STOP.
 * If the iteration function returns CONTINUE for each and every array element, return NULL.
 * The iteration function is not guaranteed to be called for each and every element in the given array.
 */
static struct json_object *
json_array_find(keystone_context_t *context, struct json_object *array, item_callback_func_t callback, void *callback_arg)
{
	struct json_object *item;
	unsigned int i;

	assert(context != NULL);
	assert(array != NULL);
	assert(callback != NULL);

	i = json_array_length(context, array);
	while (i--) {
		item_callback_return_t ret;
		item = json_array_get(context, array, i);
		ret = callback(context, item, callback_arg);
		switch (ret) {
		case CONTINUE:
			break;
		case STOP:
			return item;
		default:
			assert(0);
			break;
		}
	}

	return NULL;
}

const char *
service_name(unsigned int service)
{
	assert(service < ELEMENTSOF(openstack_service_names));
	return openstack_service_names[service];
}

const char *
endpoint_url_name(unsigned int endpoint)
{
	assert(endpoint < ELEMENTSOF(openstack_service_endpoint_url_type_friendly_names));
	return openstack_service_endpoint_url_type_friendly_names[endpoint];
}

/**
 * If the OpenStack service represented by the given JSON object is of the type name given by callback_arg, return STOP.
 * Otherwise, if it is of some other type or if its type cannot be determined, return CONTINUE.
 */
static item_callback_return_t
filter_service_by_type(keystone_context_t *context, struct json_object *service, void *callback_arg)
{
	const char *desired_type = (const char *) callback_arg;
	struct json_object *service_type;

	assert(context != NULL);
	assert(service != NULL);
	assert(callback_arg != NULL);

	if (!json_object_is_type(service, json_type_object)) {
		context->keystone_error("response.access.serviceCatalog[n] is not an object", KSERR_PARSE);
		return CONTINUE;
	}
	if (!json_object_object_get_ex(service, "type", &service_type)) {
		context->keystone_error("response.access.serviceCatalog[n] lacks a 'type' key", KSERR_PARSE);
		return CONTINUE;
	}
	if (!json_object_is_type(service_type, json_type_string)) {
		context->keystone_error("response.access.serviceCatalog[n].type is not a string", KSERR_PARSE);
		return CONTINUE;
	}
	if (0 != strcmp(json_object_get_string(service_type), desired_type)) {
		return CONTINUE; /* Not the service type we're after */
	}

	return STOP; /* Acceptable */
}

/**
 * Given a JSON array representing a list of OpenStack services, find the first service of the given-named type.
 * If a service of the given type name is found, return it.
 * Otherwise, if no service of the given type name is found, return NULL.
 */
struct json_object *
find_service_by_type_name(keystone_context_t *context, struct json_object *services, const char *desired_type)
{
	assert(context != NULL);
	assert(services != NULL);
	assert(desired_type != NULL);

	return json_array_find(context, services, filter_service_by_type, (void *) desired_type);
}

/**
 * Given a JSON array representing a list of OpenStack services, find the first service of the given type.
 * Otherwise, if a service of the given type is found, return it.
 * Otherwise, if no service of the given type is found, return NULL.
 */
struct json_object *
find_service_by_type(keystone_context_t *context, struct json_object *services, enum openstack_service desired_type)
{
	assert(context != NULL);
	assert(services != NULL);
	assert((unsigned int) desired_type < ELEMENTSOF(openstack_service_names));
	assert(openstack_service_names[(unsigned int) desired_type] != NULL);

	return json_array_find(context, services, filter_service_by_type, (void *) openstack_service_names[(unsigned int) desired_type]);
}

/**
 * Given a JSON object representing an endpoint of an OpenStack service and a desired endpoint version,
 * if the given endpoint appears to have the given API version, or it has no versionID attribute, return STOP.
 * Otherwise, if the endpoint's version is not the desired version, or the endpoint's version is erroneous, return CONTINUE.
 */
static item_callback_return_t
filter_endpoint_by_version(keystone_context_t *context, json_object *endpoint, void *callback_arg)
{
	unsigned int desired_api_version;
	struct json_object *endpoint_api_version;

	assert(context != NULL);
	assert(endpoint != NULL);
	assert(callback_arg != NULL);

	desired_api_version = *((unsigned int *) callback_arg);
	if (!json_object_is_type(endpoint, json_type_object)) {
		context->keystone_error("response.access.serviceCatalog[n].endpoints[n] is not an object", KSERR_PARSE);
		return CONTINUE;
	}
	if (!json_object_object_get_ex(endpoint, "versionId", &endpoint_api_version)) {
		/* Keystone documentation includes a versionID key, but it is not present in the responses I've seen */
		/* context->keystone_error("response.access.serviceCatalog[n].endpoints[n] lacks a 'versionId' key", KSERR_PARSE); */
		return STOP; /* Take a lack of versionID to mean a catch-all */
	}
	if (!json_object_is_type(endpoint_api_version, json_type_string)) {
		context->keystone_error("response.access.serviceCatalog[n].endpoints[n].versionId is not a string", KSERR_PARSE);
		return CONTINUE; /* Version attribute wrong type */
	}
	if (json_object_get_double(endpoint_api_version) != desired_api_version) {
		return CONTINUE; /* Not the version we're after */
	}

	/* Found the API version we're after */
	return STOP;
}

/**
 * Given a JSON object representing an OpenStack service, find the first endpoint of the given version.
 * If an endpoint of the given version is found, return it.
 * Otherwise, if no endpoint of the given version is found, return NULL.
 */
static struct json_object *
service_find_endpoint_by_version(keystone_context_t *context, struct json_object *service, unsigned int desired_api_version)
{
	struct json_object *endpoints, *endpoint;

	if (json_object_object_get_ex(service, "endpoints", &endpoints)) {
		if (0 == desired_api_version) {
			/* No desired API version currently set, so use the first endpoint found */
			endpoint = json_array_get(context, endpoints, 0);
		} else {
			/* Looking for a certain version of the Swift RESTful API */
			endpoint = json_array_find(context, endpoints, filter_endpoint_by_version, (void *) &desired_api_version);
		}
	} else {
		context->keystone_error("response.access.serviceCatalog[n] lacks an 'endpoints' key", KSERR_PARSE);
		endpoint = NULL; /* Lacking the expected key */
	}

	return endpoint;
}

/**
 * Given a JSON object representing an OpenStack service endpoint, return its URL of the given type, if any.
 * If the service endpoint has no URL
 */
static const char *
endpoint_url(keystone_context_t *context, struct json_object *endpoint, enum openstack_service_endpoint_url_type endpoint_url_type)
{
	struct json_object *endpoint_public_url;
	const char *url_val;
	const char *url_type_name;

	assert(context != NULL);
	assert(endpoint != NULL);
	assert(endpoint_url_type < ELEMENTSOF(openstack_service_endpoint_url_type_names));
	assert(openstack_service_endpoint_url_type_names[(unsigned int) endpoint_url_type] != NULL);

	url_type_name = openstack_service_endpoint_url_type_names[(unsigned int) endpoint_url_type];

	if (json_object_object_get_ex(endpoint, url_type_name, &endpoint_public_url)) {
		if (json_object_is_type(endpoint_public_url, json_type_string)) {
			url_val = json_object_get_string(endpoint_public_url);
		} else {
			context->keystone_error("response.access.serviceCatalog[n].endpoints[n] URL is not a string", KSERR_PARSE);
			url_val = NULL;
		}
	} else {
		context->keystone_error("response.access.serviceCatalog[n].endpoints[n] lacks a URL key of the requested type", KSERR_PARSE);
		url_val = NULL;
	}

	return url_val;
}

/**
 * Given a desired service type and version and type of URL, find a service of the given type in Keystone's catalog of services,
 * then find an endpoint of that service with the given API version, then return its URL of the given type.
 * Return NULL if the service cannot be found, or if no endpoint of the given version can be found,
 * or if the service endpoint of the given version has no URL of the given type.
 */
const char *
keystone_get_service_url(keystone_context_t *context, enum openstack_service desired_service_type, unsigned int desired_api_version, enum openstack_service_endpoint_url_type endpoint_url_type)
{
	struct json_object *service;
	const char *url;

	assert(context != NULL);

	service = find_service_by_type(context, context->pvt.services, desired_service_type);
	if (service) {
		static struct json_object *endpoint;
		endpoint = service_find_endpoint_by_version(context, service, desired_api_version);
		if (endpoint) {
			url = endpoint_url(context, endpoint, endpoint_url_type);
		} else {
			url = NULL;
		}
	} else {
		url = NULL;
	}

	return url;
}

/**
 * Retrieve the authentication token and service catalog from a now-complete Keystone JSON response,
 * and store them in the Keystone context structure for later use.
 */
static enum keystone_error
process_keystone_json(keystone_context_t *context, struct json_object *response)
{
	struct json_object *access, *token, *id;

	if (context->pvt.debug) {
                char stderrs[20] = "/dev/stderr";
		json_object_to_file_ext(stderrs, response, JSON_C_TO_STRING_PRETTY);
	}
	if (!json_object_is_type(response, json_type_object)) {
		context->keystone_error("response is not an object", KSERR_PARSE);
		return KSERR_PARSE; /* Not the expected JSON object */
	}
	/* Everything is in an "access" sub-object */
	if (!json_object_object_get_ex(response, "access", &access)) {
		context->keystone_error("response lacks 'access' key", KSERR_PARSE);
		return KSERR_PARSE; /* Lacking the expected key */
	}
	if (!json_object_is_type(access, json_type_object)) {
		context->keystone_error("response.access is not an object", KSERR_PARSE);
		return KSERR_PARSE; /* Not the expected JSON object */
	}
	/* Service catalog */
	if (!json_object_object_get_ex(access, "serviceCatalog", &context->pvt.services)) {
		context->keystone_error("response.access lacks 'serviceCatalog' key", KSERR_PARSE);
		return KSERR_PARSE;
	}
	if (!json_object_is_type(context->pvt.services, json_type_array)) {
		context->keystone_error("response.access.serviceCatalog not an array", KSERR_PARSE);
		return KSERR_PARSE;
	}
	/* Authentication token */
	if (!json_object_object_get_ex(access, "token", &token)) {
		context->keystone_error("reponse.access lacks 'token' key", KSERR_PARSE);
		return KSERR_PARSE; /* Lacking the expected key */
	}
	if (!json_object_is_type(token, json_type_object)) {
		context->keystone_error("response.access.token is not an object", KSERR_PARSE);
		return KSERR_PARSE; /* Not the expected JSON object */
	}
	if (!json_object_object_get_ex(token, "id", &id)) {
		context->keystone_error("response.access.token lacks 'id' key", KSERR_PARSE);
		return KSERR_PARSE; /* Lacking the expected key */
	}
	if (!json_object_is_type(id, json_type_string)) {
		context->keystone_error("response.access.token.id is not a string", KSERR_PARSE);
		return KSERR_PARSE; /* Not the expected JSON string */
	}
	context->pvt.auth_token = (char *)context->allocator(
		context->pvt.auth_token,
		json_object_get_string_len(id)
		+ 1 /* '\0' */
	);
	if (NULL == context->pvt.auth_token) {
		return KSERR_PARSE; /* Allocation failed */
	}
	strcpy(context->pvt.auth_token, json_object_get_string(id));

	return KSERR_SUCCESS;
}

/**
 * Process a Keystone authentication response.
 * This parses the response and saves copies of the interesting service endpoint URLs.
 */
static size_t
process_keystone_response(void *ptr, size_t size, size_t nmemb, void *userdata)
{
	keystone_context_t *context = (keystone_context_t *) userdata;
	const char *body = (const char *) ptr;
	size_t len = size * nmemb;
	struct json_object *jobj;
	enum json_tokener_error json_err;

	assert(context->pvt.json_tokeniser != NULL);

	jobj = json_tokener_parse_ex(context->pvt.json_tokeniser, body, len);
	json_err = json_tokener_get_error(context->pvt.json_tokeniser);
	if (json_tokener_success == json_err) {
		enum keystone_error sc_err = process_keystone_json(context, jobj);
		if (sc_err != KSERR_SUCCESS) {
			return 0; /* Failed to process JSON. Inform libcurl no data 'handled' */
		}
	} else if (json_tokener_continue == json_err) {
		/* Complete JSON response not yet received; continue */
	} else {
		context->json_error("json_tokener_parse_ex", json_err);
		context->keystone_error("failed to parse response", KSERR_PARSE);
		return 0; /* Apparent JSON parsing problem. Inform libcurl no data 'handled' */
	}

	return len; /* Inform libcurl that all data were 'handled' */
}

/**
 * Authenticate against a Keystone authentication service with the given tenant and user names and password.
 * This yields an authorisation token, which is then used to access all Swift services.
 */
enum keystone_error
keystone_authenticate(keystone_context_t *context, const char *url, const char *tenant_name, const char *username, const char *password)
{
	CURLcode curl_err;
	struct curl_slist *headers = NULL;
	size_t body_len;

	assert(context != NULL);
	assert(context->pvt.curl != NULL);
	assert(url != NULL);
	assert(tenant_name != NULL);
	assert(username != NULL);
	assert(password != NULL);

	body_len =
		strlen(KEYSTONE_AUTH_PAYLOAD_BEFORE_USERNAME)
		+ strlen(username)
		+ strlen(KEYSTONE_AUTH_PAYLOAD_BEFORE_PASSWORD)
		+ strlen(password)
		+ strlen(KEYSTONE_AUTH_PAYLOAD_BEFORE_TENANT)
		+ strlen(tenant_name)
		+ strlen(KEYSTONE_AUTH_PAYLOAD_END)
	;

	/* Create or reset the JSON tokeniser */
	if (NULL == context->pvt.json_tokeniser) {
		context->pvt.json_tokeniser = json_tokener_new();
		if (NULL == context->pvt.json_tokeniser) {
			context->keystone_error("json_tokener_new failed", KSERR_INIT_FAILED);
			return KSERR_INIT_FAILED;
		}
	} else {
		json_tokener_reset(context->pvt.json_tokeniser);
	}

	curl_err = curl_easy_setopt(context->pvt.curl, CURLOPT_URL, url);
	if (CURLE_OK != curl_err) {
		context->curl_error("curl_easy_setopt", curl_err);
		return KSERR_URL_FAILED;
	}

	curl_err = curl_easy_setopt(context->pvt.curl, CURLOPT_POST, 1L);
	if (CURLE_OK != curl_err) {
		context->curl_error("curl_easy_setopt", curl_err);
		return KSERR_URL_FAILED;
	}

	/* Append header specifying body content type (since this differs from libcurl's default) */
	headers = curl_slist_append(headers, "Content-Type: " KEYSTONE_AUTH_REQUEST_FORMAT);

	/* Append pseudo-header defeating libcurl's default addition of an "Expect: 100-continue" header. */
	headers = curl_slist_append(headers, "Expect:");

	/* Generate POST request body containing the authentication credentials */
	context->pvt.auth_payload = (char *)context->allocator(
		context->pvt.auth_payload,
		body_len
		+ 1 /* '\0' */
	);
	if (NULL == context->pvt.auth_payload) {
		curl_slist_free_all(headers);
		return KSERR_ALLOC_FAILED;
	}
	sprintf(context->pvt.auth_payload, "%s%s%s%s%s%s%s",
		KEYSTONE_AUTH_PAYLOAD_BEFORE_USERNAME,
		username,
		KEYSTONE_AUTH_PAYLOAD_BEFORE_PASSWORD,
		password,
		KEYSTONE_AUTH_PAYLOAD_BEFORE_TENANT,
		tenant_name,
		KEYSTONE_AUTH_PAYLOAD_END
	);

	if (context->pvt.debug) {
		fputs(context->pvt.auth_payload, stderr);
	}

	/* Pass the POST request body to libcurl. The data are not copied, so they must persist during the request lifetime. */
	curl_err = curl_easy_setopt(context->pvt.curl, CURLOPT_POSTFIELDS, context->pvt.auth_payload);
	if (CURLE_OK != curl_err) {
		context->curl_error("curl_easy_setopt", curl_err);
		curl_slist_free_all(headers);
		return KSERR_URL_FAILED;
	}

	curl_err = curl_easy_setopt(context->pvt.curl, CURLOPT_POSTFIELDSIZE, body_len);
	if (CURLE_OK != curl_err) {
		context->curl_error("curl_easy_setopt", curl_err);
		curl_slist_free_all(headers);
		return KSERR_URL_FAILED;
	}

	/* Add header requesting desired response content type */
	headers = curl_slist_append(headers, "Accept: " KEYSTONE_AUTH_RESPONSE_FORMAT);

	curl_err = curl_easy_setopt(context->pvt.curl, CURLOPT_HTTPHEADER, headers);
	if (CURLE_OK != curl_err) {
		context->curl_error("curl_easy_setopt", curl_err);
		curl_slist_free_all(headers);
		return KSERR_URL_FAILED;
	}

	curl_err = curl_easy_setopt(context->pvt.curl, CURLOPT_WRITEFUNCTION, process_keystone_response);
	if (CURLE_OK != curl_err) {
		context->curl_error("curl_easy_setopt", curl_err);
		curl_slist_free_all(headers);
		return KSERR_URL_FAILED;
	}

	curl_err = curl_easy_setopt(context->pvt.curl, CURLOPT_WRITEDATA, context);
	if (CURLE_OK != curl_err) {
		context->curl_error("curl_easy_setopt", curl_err);
		curl_slist_free_all(headers);
		return KSERR_URL_FAILED;
	}

	curl_err = curl_easy_perform(context->pvt.curl);
	if (CURLE_OK != curl_err) {
		context->curl_error("curl_easy_perform", curl_err);
		curl_slist_free_all(headers);
		return KSERR_URL_FAILED;
	}

	curl_slist_free_all(headers);

	if (NULL == context->pvt.auth_token) {
		return KSERR_AUTH_REJECTED;
	}

	return KSERR_SUCCESS;
}

/**
 * Return the previously-acquired Keystone authentication token, if any.
 * If no authentication token has previously been acquired, return NULL.
 */
const char *
keystone_get_auth_token(keystone_context_t *context)
{
	assert(context != NULL);

	return context->pvt.auth_token;
}
