#ifndef KEYSTONE_CLIENT_H_
#define KEYSTONE_CLIENT_H_

#include <stdio.h>
#include <malloc.h>
#include <curl/curl.h>
#include <json-c/json.h>

/**
 * High-level types of errors which can occur while attempting to use Keystone.
 * More detail is available from lower-level libraries (such as curl and libjson)
 * using error callbacks specific to those libraries.
 */
enum keystone_error {
	KSERR_SUCCESS       = 0, /* Success */
	KSERR_INIT_FAILED   = 1, /* Initialisation of this library failed */
	KSERR_INVARG        = 2, /* Invalid argument */
	KSERR_ALLOC_FAILED  = 3, /* Memory allocation failed */
	KSERR_URL_FAILED    = 4, /* Network operation on a URL failed */
	KSERR_AUTH_REJECTED = 5, /* Authentication attempt rejected */
	KSERR_NOTFOUND      = 6, /* Requested service(s) not found in service catalog */
	KSERR_PARSE         = 7  /* Failed to parse Keystone response */
};

/**
 * Types of OpenStack service.
 */
enum openstack_service {
	OS_SERVICE_KEYSTONE = 0, /* Keystone authentication and service catalog */
	OS_SERVICE_NOVA     = 1, /* Nova compute */
	OS_SERVICE_NOVA_EC2 = 2, /* Nova compute, EC2 API */
	OS_SERVICE_SWIFT    = 3, /* Swift object storage */
	OS_SERVICE_SWIFT_S3 = 4, /* Swift object storage, S3 API */
	OS_SERVICE_CINDER   = 5, /* Cinder block storage */
	OS_SERVICE_GLANCE   = 6  /* Glance image storage */
};
#define OS_SERVICE_MAX OS_SERVICE_GLANCE

/**
 * Types of OpenStack service endpoint.
 */
enum openstack_service_endpoint_url_type {
	OS_ENDPOINT_URL_PUBLIC   = 0, /* Service's public endpoint */
	OS_ENDPOINT_URL_PRIVATE  = 1, /* Service's private endpoint */
	OS_ENDPOINT_URL_INTERNAL = 2, /* Service's internal endpoint */
};
#define OS_ENDPOINT_URL_MAX OS_ENDPOINT_URL_INTERNAL

/* keystone client library's per-thread private context */
struct keystone_context_private {
	CURL *curl;       /* Handle to curl library's easy interface */
	unsigned int debug;
	struct json_tokener *json_tokeniser; /* libjson0 library's JSON tokeniser */
	struct json_object *services; /* service catalog JSON array */
	unsigned int verify_cert_trusted;  /* True if the peer's certificate must chain to a trusted CA, false otherwise */
	unsigned int verify_cert_hostname; /* True if the peer's certificate's hostname must be correct, false otherwise */
	char *auth_payload; /* Authentication POST payload, containing credentials */
	char *auth_token; /* Authentication token previously obtained from Keystone */
};

typedef struct keystone_context_private keystone_context_private_t;

/* A function which allocates, re-allocates or de-allocates memory */
typedef void *(*keystone_allocator_func_t)(void *ptr, size_t newsize);

/* A function which receives curl errors */
typedef void (*curl_error_callback_t)(const char *curl_funcname, CURLcode res);

/* A function which receives libjson errors */
typedef void (*json_error_callback_t)(const char *json_funcname, enum json_tokener_error json_err);

/* A function which receives Keystone errors */
typedef void (*keystone_error_callback_t)(const char *keystone_operation, enum keystone_error keystone_err);

/**
 * All use of this library is performed within a 'context'.
 * Contexts cannot be shared among threads; each thread must have its own context.
 * Your program is responsible for allocating and freeing context structures.
 * Contexts should be zeroed out prior to use.
 */
struct keystone_context {

	/* These members are 'public'; your program can (and should) set them at will */

	/**
	 * Called when a libcurl error occurs.
	 * Your program may set this function pointer in order to perform custom error handling.
	 * If this is NULL at the time keystone_start is called, a default handler will be used.
	 */
	curl_error_callback_t curl_error;
	/**
	 * Called when a libjson error occurs.
	 * Your program may set this function in order to perform custom error handling.
	 * If this is NULL at the time keystone_start is called, a default handler will be used.
	 */
	json_error_callback_t json_error;
	/**
	 * Called when a Keystone error occurs.
	 * Your program may set this function in order to perform custom error handling.
	 * If this is NULL at the time keystone_start is called, a default handler will be used.
	 */
	keystone_error_callback_t keystone_error;
	/**
	 * Called when this library needs to allocate, re-allocate or free memory.
	 * If size is zero and ptr is NULL, nothing is done.
	 * If size is zero and ptr is non-NULL, the previously-allocated memory at ptr is to be freed.
	 * If size is non-zero and ptr is NULL, memory of the given size is to be allocated.
	 * If size is non-zero and ptr is non-NULL, the previously-allocated memory at ptr
	 * is to be re-allocated to be the given size.
	 * If this function pointer is NULL at the time keystone_start is called, a default re-allocator will be used.
	 */
	keystone_allocator_func_t allocator;
	/* This member (and its members, recursively) are 'private'. */
	/* They should not be modified by your program unless you *really* know what you're doing. */
	keystone_context_private_t pvt;
};

typedef struct keystone_context keystone_context_t;

/**
 * Begin using this library.
 * The context passed must be zeroed out, except for the public part,
 * in which you may want to over-ride the function pointers.
 * Function pointers left NULL will be given meaningful defaults.
 * This must be called early in the execution of your program,
 * before additional threads (if any) are created.
 * This must be called before any other use of this library by your program.
 * These restrictions are imposed by libcurl, and the libcurl restrictions are in turn
 * imposed by the libraries that libcurl uses.
 * If your program is a library, it will need to expose a similar API to,
 * and expose similar restrictions on, its users.
 */
enum keystone_error keystone_global_init(void);

/**
 * Cease using this library.
 * This must be called late in the execution of your program,
 * after all secondary threads (if any) have exited,
 * so that there is precisely one thread in your program at the time of the call.
 * This library must not be used by your program after this function is called.
 * This function must be called exactly once for each successful prior call to keystone_global_init
 * by your program.
 * These restrictions are imposed by libcurl, and the libcurl restrictions are in turn
 * imposed by the libraries that libcurl uses.
 * If your program is a library, it will need to expose a similar API to,
 * and expose similar restrictions on, its users.
 */
void keystone_global_cleanup(void);

/**
 * Begin using this library for a single thread of your program.
 * This must be called by each thread of your program in order to use this library.
 */
enum keystone_error keystone_start(keystone_context_t *context);

/**
 * Cease using this library for a single thread.
 * This must be called by each thread of your program after it is finished using this library.
 * Each thread in your program must call this function precisely once for each successful prior call
 * to keystone_start by that thread.
 * After this call, the context is invalid.
 */
void keystone_end(keystone_context_t *context);

/**
 * Control whether a proxy (eg HTTP or SOCKS) is used to access the Keystone server.
 * Argument must be a URL, or NULL if no proxy is to be used.
 */
enum keystone_error keystone_set_proxy(keystone_context_t *context, const char *proxy_url);

/**
 * Control verbose logging to stderr of the actions of this library and the libraries it uses.
 * Currently this enables logging to standard error of libcurl's actions.
 */
enum keystone_error keystone_set_debug(keystone_context_t *context, unsigned int enable_debugging);

const char *service_name(unsigned int service);
const char *endpoint_url_name(unsigned int endpoint);

/**
 * Authenticate against a Keystone authentication service with the given tenant and user names and password.
 * This yields an authorisation token, which is then used to access all Swift services.
 */
enum keystone_error keystone_authenticate(keystone_context_t *context, const char *url, const char *tenant_name, const char *username, const char *password);

/**
 * Return the previously-acquired Keystone authentication token, if any.
 * If no authentication token has previously been acquired, return NULL.
 */
const char *keystone_get_auth_token(keystone_context_t *context);

/**
 * Given a desired service type and version and type of URL, find a service of the given type in Keystone's catalog of services,
 * then find an endpoint of that service with the given API version, then return its URL of the given type.
 * Return NULL if the service cannot be found, or if no endpoint of the given version can be found,
 * or if the service endpoint of the given version has no URL of the given type.
 */
const char *keystone_get_service_url(keystone_context_t *context, enum openstack_service desired_service_type, unsigned int desired_api_version, enum openstack_service_endpoint_url_type endpoint_url_type);

#endif /* KEYSTONE_CLIENT_H_ */
