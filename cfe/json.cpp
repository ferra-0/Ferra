#include <json.hpp>
using json = nlohmann::json;

extern "C" {

json* json_parse(const char* text) {
	try {
		return new json(json::parse(text));
	} catch (...) {
		return nullptr;
	}
}

void json_free(json* j) {
	delete j;
}

const char* json_dump(json* j) {
	static std::string out;

	if (!j) return "";

	out = j->dump();
	return out.c_str();
}

json* json_get(json* j, const char* key) {
	if (!j) return nullptr;

	auto it = j->find(key);

	if (it == j->end()) return nullptr;

	return new json(*it);
}

const char* json_string(json* j) {
	static std::string out;

	if (!j || !j->is_string()) return "";

	out = j->get<std::string>();

	return out.c_str();
}

double json_number(json* j) {
	if (!j) return 0.0;

	return j->get<double>();
}

bool json_bool(json* j) {
	if (!j) return false;

	return j->get<bool>();
}

long long json_size(json* j) {
	if (!j) return 0;

	return j->size();
}

json* json_at(json* j, long long index) {
	if (!j || !j->is_array() || index < 0 ||
			static_cast<std::size_t>(index) >= j->size()) {
		return nullptr;
	}

	return new json((*j)[index]);
}

const char* json_key_at(json* j, long long index) {
	static std::string out;

	if (!j || !j->is_object() || index < 0 ||
			static_cast<std::size_t>(index) >= j->size()) {
		return nullptr;
	}

	auto it = j->begin();
	std::advance(it, index);
	out = it.key();
	return out.c_str();
}

json* json_object() {
	return new json(json::object());
}

json* json_array() {
	return new json(json::array());
}

void json_set(json* j, const char* key, json* value) {
	if (!j || !key || !value || !j->is_object()) return;
	(*j)[key] = *value;
}

void json_push(json* j, json* value) {
	if (!j || !value || !j->is_array()) return;
	j->push_back(*value);
}

bool json_is_null(json* j) {
	return !j || j->is_null();
}

int json_type(json* j) {
	if (!j) return 0;

	if (j->is_null()) return 0;
	if (j->is_boolean()) return 1;
	if (j->is_number()) return 2;
	if (j->is_string()) return 3;
	if (j->is_array()) return 4;
	if (j->is_object()) return 5;

	return -1;
}

json* json_new_string(const char* value) {
	if (!value) {
		return new json("");
	}

	return new json(value);
}

json* json_new_number(double value) {
	return new json(value);
}

json* json_new_bool(bool value) {
	return new json(value);
}

json* json_new_null() {
	return new json(nullptr);
}

}
