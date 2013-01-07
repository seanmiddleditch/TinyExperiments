#include <vector>
#include <map>
#include <cassert>
#include <cstring>

// very slow id-object map
namespace v1
{
	struct object {
		object() {}
		object(int id) : id(id) {}

		int id;
		// other fields
	};

	int next_id = 0;
	std::map<int, object> object_table;

	int create_object() {
		object_table[next_id] = object(next_id);
		return next_id++;
	}

	object* get_object(int id) {
		auto iter = object_table.find(id);
		return iter == object_table.end() ? nullptr : &iter->second;
	}

	void destroy_object(int id) {
		auto iter = object_table.find(id);
		if (iter != object_table.end())
			object_table.erase(iter);
	}
}

// incomplete id-object map
namespace v2 {
	struct object {
		object(int id) : id(id) {}

		int id;
		// other fields
	};

	std::vector<object> object_table;
	std::vector<int> free_list;

	int create_object() {
		if (!free_list.empty()) {
			int free = free_list.back();
			free_list.pop_back();
			object_table[free].id = free;
			return free;
		} else {
			int id = object_table.size();
			object_table.push_back(object(id));
			return id;
		}
	}

	object* get_object(int id) {
		return object_table[id].id == -1 ? nullptr : &object_table[id];
	}

	void destroy_object(int id) {
		object_table[id].id = -1;
		free_list.push_back(id);
	}
}

// fast object table with id recycling bug unfixed
namespace v3 {
	struct object {
		int id;

		// other fields
	};

	const size_t chunk_size = 256;
	std::vector<object*> object_table;
	std::vector<int> free_list;

	int create_object() {
		if (free_list.empty()) {
			object* chunk = new object[chunk_size];
			for (int i = chunk_size - 1; i >= 0; --i)
				free_list.push_back(object_table.size() * chunk_size + i);
			object_table.push_back(chunk);
		}

		int free = free_list.back();
		free_list.pop_back();
		object_table[free / chunk_size][free % chunk_size].id = free;
		return free;
	}

	object* get_object(int id) {
		object* obj = object_table[id / chunk_size] + (id % chunk_size);
		return obj->id == -1 ? nullptr : obj;
	}

	void destroy_object(int id) {
		get_object(id)->id = -1;
		free_list.push_back(id);
	}
}

// complete simplified slot map
namespace v4 {
	typedef long long object_id;

	struct object {
		object_id id;

		// other fields
	};

	const size_t chunk_size = 256;
	std::vector<object*> object_table;
	std::vector<int> free_list;

	object_id create_object() {
		if (free_list.empty()) {
			object* chunk = new object[chunk_size];
			for (int i = chunk_size - 1; i >= 0; --i) {
				chunk[i].id = object_table.size() * chunk_size + i;
				free_list.push_back(object_table.size() * chunk_size + i);
			}
			object_table.push_back(chunk);
		}

		int free = free_list.back();
		free_list.pop_back();
		return object_table[free / chunk_size][free % chunk_size].id;
	}

	object* get_object(object_id id) {
		object* obj = object_table[(id & 0xFFFFFFFF) / chunk_size] + ((id & 0xFFFFFFFF) % chunk_size);
		return obj->id != id ? nullptr : obj;
	}

	void destroy_object(object_id id) {
		object* obj = get_object(id);
		obj->id = (obj->id & 0xFFFFFFFF) | (((obj->id >> 32) + 1) << 32);
		free_list.push_back(id & 0xFFFFFFFF);
	}
}

// exceedingly NON-exhaustive test case
int main()
{
	std::vector<int> v1_ids;
	std::vector<int> v2_ids;
	std::vector<int> v3_ids;
	std::vector<v4::object_id> v4_ids;

	for (int j = 0; j < 20; ++j)
	{
		for (int i = 0; i < 1000; ++i)
		{
			v1_ids.push_back(v1::create_object());
			v2_ids.push_back(v2::create_object());
			v3_ids.push_back(v3::create_object());
			v4_ids.push_back(v4::create_object());
		}

		for (int i = 0; i < 1000; ++i)
		{
			assert(v1::get_object(v1_ids[i]) != nullptr);
			assert(v2::get_object(v2_ids[i]) != nullptr);
			assert(v3::get_object(v3_ids[i]) != nullptr);
			assert(v4::get_object(v4_ids[i]) != nullptr);

			assert(v1::get_object(v1_ids[i])->id == v1_ids[i]);
			assert(v2::get_object(v2_ids[i])->id == v2_ids[i]);
			assert(v3::get_object(v3_ids[i])->id == v3_ids[i]);
			assert(v4::get_object(v4_ids[i])->id == v4_ids[i]);
		}

		for (int i = 0; i < 1000; ++i)
		{
			v1::destroy_object(v1_ids[i]);
			v2::destroy_object(v2_ids[i]);
			v3::destroy_object(v3_ids[i]);
			v4::destroy_object(v4_ids[i]);
		}

		for (int i = 0; i < 1000; ++i)
		{
			assert(v1::get_object(v1_ids[i]) == nullptr);
			assert(v2::get_object(v2_ids[i]) == nullptr);
			assert(v3::get_object(v3_ids[i]) == nullptr);
			assert(v4::get_object(v4_ids[i]) == nullptr);
		}

		v1_ids.clear();
		v2_ids.clear();
		v3_ids.clear();
		v4_ids.clear();
	}
}
