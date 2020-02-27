package models

// GetValue search for specified key and returns its value.
func (kvps *KeyValuePairs) GetValue(key string) string {
	for _, kvp := range kvps.GetKeyValuePair() {
		if kvp.GetKey() == key {
			return kvp.GetValue()
		}
	}
	return ""
}
