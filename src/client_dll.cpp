#include <stdio.h>
#include <cstdint>
#include <atomic>
#include <json.hpp>
//#include <gzip.hpp>

#include "router.h"
#include <intercept.hpp>
#include "client.h"
#include <any>
#include <fstream>

using namespace intercept::types;
using json = nlohmann::json;


enum class EntryType {
    array,
    clas,
    flt,
    int32,
    int64,
    string,
    boolean
};

class arrayEntry : public param_archive_array_entry {
public:
    arrayEntry(EntryType type_, std::variant<float, int, int64_t, r_string, bool> val_) : type(type_), value(val_) {}
    ~arrayEntry() override {

    }
    operator float() const override {
        return std::get<float>(value);
    }
    operator int() const override {
        if (type == EntryType::int32)
            return std::get<int>(value);
        if (type == EntryType::int64)
            return std::get<int64_t>(value);
    }
    operator const intercept::types::r_string() const override {
        return std::get<r_string>(value);
    }
    operator intercept::types::r_string() const override {
        return std::get<r_string>(value);
    }
    operator bool() const override {
        return std::get<bool>(value);
    }
    EntryType type;
    std::variant<float,int,int64_t, r_string,bool> value;
};

class JArrayEntry : public param_archive_array_entry {
public:
    JArrayEntry(json* val_) : value(val_) {}
    ~JArrayEntry() override {

    }
    operator float() const override {
        return *value;
    }
    operator int() const override {
        return *value;  
    }
    operator const intercept::types::r_string() const override {
        return r_string(value->get<std::string>());
    }
    operator intercept::types::r_string() const override {
        return r_string(value->get<std::string>());
    }
    operator bool() const override {
        return *value;
    }
    json* value;
};


class arrayEntryRef : public param_archive_array_entry {
public:

    static void* operator new(const std::size_t sz_) {
        return (void*) rv_allocator<arrayEntryRef>::create_single(nullptr);
    }
    static void operator delete(void* ptr_, std::size_t sz_) {
        rv_allocator<arrayEntryRef>::deallocate((arrayEntryRef*) ptr_, sz_);
    }



    param_archive_array_entry* value;

    arrayEntryRef(std::shared_ptr<arrayEntry> v) :value(v.get()) {}
    arrayEntryRef(std::nullptr_t v) :value(v) {}
    arrayEntryRef(std::shared_ptr<JArrayEntry> v) :value(v.get()) {}

    virtual ~arrayEntryRef() override {
  
    }
    virtual operator float() const override {
        return value->operator float();
    }
    virtual operator int() const override {
        return value->operator int();
    }
    virtual operator const intercept::types::r_string() const override {
        return value->operator const r_string();
    }
    virtual operator intercept::types::r_string() const override {
        return value->operator r_string();
    }
    virtual operator bool() const override {
        return value->operator bool();
    }
};


class ClassEntryx;
class ClassEntryJson;

class ClassEntryRef : public param_archive_entry {
public:

    static void* operator new(const std::size_t sz_) {
        return (void*) rv_allocator<ClassEntryRef>::create_single(nullptr);
    }
    static void operator delete(void* ptr_, std::size_t sz_) {
        rv_allocator<ClassEntryRef>::deallocate((ClassEntryRef*) ptr_, sz_);
    }



    param_archive_entry* value;
    
    ClassEntryRef(std::nullptr_t v) :value(v) {}
    ClassEntryRef(std::shared_ptr<ClassEntryx> v);
    ClassEntryRef(std::shared_ptr<ClassEntryJson> v);

    //! virtual destructor
    virtual ~ClassEntryRef() {

    }

    // generic entry
    int entry_count() const override {
        return value->entry_count();
    }

    param_archive_entry *get_entry_by_index(int i) const override {
        return value->get_entry_by_index(i);
    }

    r_string current_entry_name() override { return value->current_entry_name(); }
    param_archive_entry *get_entry_by_name(const r_string &name) const override { return value->get_entry_by_name(name); }
    operator float() const override { return value->operator float(); }
    operator int() const override { return value->operator int(); }

    operator int64_t() const override { return value->operator int64_t(); }

    operator r_string() const override { return value->operator r_string(); }

    operator bool() const override { return value->operator bool(); }
    //GetContext
    r_string _placeholder1(uint32_t member = NULL) const override { return value->_placeholder1(member); }

    // array
    void reserve(int count) override {
        return value->reserve(count);
    }
    void add_array_entry(float val) override {
        return value->add_array_entry(val);
    }

    void add_array_entry(int val) override {
        return value->add_array_entry(val);
    }

    void add_array_entry(int64_t val) override {
        return value->add_array_entry(val);
    }
    void add_array_entry(const r_string &val) override {
        return value->add_array_entry(val);
    }

    int count() const override {
        return value->count();
    }
    param_archive_array_entry *operator [] (int i) const override {
        return value->operator [](i);
    }

    // class
    virtual param_archive_entry *add_entry_class(const r_string &name, bool guaranteedUnique = false) {
        return value->add_entry_class(name, guaranteedUnique);
    }

    param_archive_entry *add_entry_array(const r_string &name) override {
        return value->add_entry_array(name);
    }

    virtual void add_entry(const r_string &name, const r_string &val) {
        return value->add_entry(name, val);
    }
    virtual void add_entry(const r_string &name, float val) {
        return value->add_entry(name, val);
    }
    virtual void add_entry(const r_string &name, int val) {
        return value->add_entry(name, val);
    }
    virtual void add_entry(const r_string &name, int64_t val) {
        return value->add_entry(name, val);
    }
    virtual void compress() {
        return value->compress();
    }

    //! Delete the entry. Note: it could be used in rare cases only!
    virtual void _placeholder(const r_string &name) { return value->_placeholder(name); }
};



#define MAKE_ENTRY_REF(x)  new ClassEntryRef(x)
#define MAKE_ARRENTRY_REF(x)  new arrayEntryRef(x)

class ClassEntryx : public param_archive_entry {
    friend class ClassEntryRef;
public:
    r_string name;
    EntryType type;
    using arrayType = auto_array<std::shared_ptr<arrayEntry>>;
    using classType = auto_array<std::shared_ptr<ClassEntryx>>;
    //If array then std::vector<std::unique_ptr<arrayEntry>>
    std::any value;

    ClassEntryx() {}
    ClassEntryx(EntryType type_, r_string name_, std::any val_) : name(name_), type(type_), value(val_) {}
    ClassEntryx(EntryType type_, r_string name_) : name(name_), type(type_) {}


    //! virtual destructor
    virtual ~ClassEntryx() {

    }

    // generic entry
    int entry_count() const override {
        std::stringstream str;
        str << this << " entryCount " << "\n";
        OutputDebugStringA(str.str().c_str());
        if (type == EntryType::array) {
            return std::any_cast<const classType&>(value).count();
        }
        if (type == EntryType::array) {
            __debugbreak();
        }
        return 0; 
    }

    param_archive_entry* get_entry_by_index(int i) const override {
        std::stringstream str;
        str << this << " get_entry_by_index " << i << "\n";
        OutputDebugStringA(str.str().c_str());
        if (type == EntryType::array) {
            auto& arr = std::any_cast<const classType&>(value);
            return MAKE_ENTRY_REF(arr[i]);
        }
        if (type == EntryType::clas) {
            __debugbreak();
        }
        __debugbreak();
        return nullptr;
    }

    r_string current_entry_name() override { return name; }
    param_archive_entry* get_entry_by_name(const r_string& name) const override {
        std::stringstream str;
        str << this << " get_entry_by_name " << name << "\n";
        OutputDebugStringA(str.str().c_str());
        if (type == EntryType::clas) {
            auto& arr = std::any_cast<const classType&>(value);

            for (auto& it : arr) {
                if (it->name == name) {
                    return MAKE_ENTRY_REF(it);
                }
            }
            return nullptr;

        }

        __debugbreak();
        return NULL;
    }
    operator float() const override { return std::any_cast<float>(value); }
    operator int() const override { return std::any_cast<int>(value); }

    operator int64_t() const override { return std::any_cast<int64_t>(value); }

    operator const r_string() const override { return std::any_cast<r_string>(value); }
private:
    operator r_string() const override { return std::any_cast<r_string>(value); }
public:
    operator bool() const override { return std::any_cast<bool>(value); }
    //GetContext
    r_string _placeholder1(uint32_t member = NULL) const override {
        return r_string();
    }

    // array
    void reserve(int count) override {
        auto& arr = std::any_cast<arrayType&>(value);

        arr.reserve(count);

        std::stringstream str;
        str << this << " ReserveArrayElements " << count << "\n";
        OutputDebugStringA(str.str().c_str());
    }
    void add_array_entry(float val) override {
        auto& arr = std::any_cast<arrayType&>(value);
        rv_allocator<arrayEntry> al;
        arr.emplace_back(std::allocate_shared<arrayEntry>(al, EntryType::flt, val));

        std::stringstream str;
        str << this << " AddValue " << val << "\n";
        OutputDebugStringA(str.str().c_str());
    }

    void add_array_entry(int val) override {
        auto& arr = std::any_cast<arrayType&>(value);
        rv_allocator<arrayEntry> al;
        arr.emplace_back(std::allocate_shared<arrayEntry>(al,EntryType::int32, val));

        std::stringstream str;
        str << this << " AddValue " << val << "\n";
        OutputDebugStringA(str.str().c_str());
    }

    void add_array_entry(int64_t val) override {
        auto& arr = std::any_cast<arrayType&>(value);
        rv_allocator<arrayEntry> al;
        arr.emplace_back(std::allocate_shared<arrayEntry>(al,EntryType::int64, val));

        std::stringstream str;
        str << this << " AddValue " << val << "\n";
        OutputDebugStringA(str.str().c_str());
    }

    //void add_array_entry(bool val) override {
    //    std::stringstream str;
    //    str << this << " AddValue " << val << "\n";
    //    OutputDebugStringA(str.str().c_str());
    //}
    void add_array_entry(const r_string &val) override {
        auto& arr = std::any_cast<arrayType&>(value);
        rv_allocator<arrayEntry> al;
        arr.emplace_back(std::allocate_shared<arrayEntry>(al,EntryType::string, static_cast<r_string>(val)));


        std::stringstream str;
        str << this << " AddValue " << val/*.data()*/ << "\n";
        OutputDebugStringA(str.str().c_str());
    }

    int count() const override {
        auto& arr = std::any_cast<const arrayType&>(value);
        std::stringstream str;
        str << this << " getArrCount " << arr.count() << "\n";
        OutputDebugStringA(str.str().c_str());
        return arr.count();
    }

    param_archive_array_entry* operator [](int i) const override {
        auto& arr = std::any_cast<const arrayType&>(value);
        std::stringstream str;
        str << this << " getArrElement " << i << "\n";
        OutputDebugStringA(str.str().c_str());
        if (i < arr.count())
            return MAKE_ARRENTRY_REF(arr[i]);
        __debugbreak();
        return new param_archive_array_entry();
    };

    // class


    param_archive_entry* add_entry_class(const r_string& name, bool guaranteedUnique = false) override {
        auto& arr = std::any_cast<classType&>(value);
        rv_allocator<arrayEntry> al;
        auto& nidx = arr.emplace_back(std::allocate_shared<ClassEntryx>(al, EntryType::clas, name, classType()));


        std::stringstream str;
        str << this << " AddClass " << name.data() << " " << guaranteedUnique << " " << nidx->get() << "\n";
        OutputDebugStringA(str.str().c_str());
        return MAKE_ENTRY_REF(*nidx);
    };

    param_archive_entry* add_entry_array(const r_string& name) override {
        auto& arr = std::any_cast<classType&>(value);

        rv_allocator<arrayEntry> al;
        auto& nidx = arr.emplace_back(std::allocate_shared<ClassEntryx>(al, EntryType::array, name, arrayType()));

        std::stringstream str;
        str << this << " AddArray " << name.data() << " " << nidx->get() << "\n";
        OutputDebugStringA(str.str().c_str());
        return MAKE_ENTRY_REF(*nidx);
    };

    virtual void add_entry(const r_string &name, const r_string &val) {
        auto& arr = std::any_cast<classType&>(value);
        rv_allocator<arrayEntry> al;
        arr.emplace_back(std::allocate_shared<ClassEntryx>(al, EntryType::string, name, static_cast<r_string>(val)));

        std::stringstream str;
        str << this << " Add " << name.data() << " " << val.data() << "\n";
        OutputDebugStringA(str.str().c_str());
    }
    virtual void add_entry(const r_string &name, float val) {
        auto& arr = std::any_cast<classType&>(value);
        rv_allocator<arrayEntry> al;
        arr.emplace_back(std::allocate_shared<ClassEntryx>(al, EntryType::flt, name, val));

        std::stringstream str;
        str << this << " Add " << name.data() << " " << val << "\n";
        OutputDebugStringA(str.str().c_str());
    }
    virtual void add_entry(const r_string &name, int val) {
        auto& arr = std::any_cast<classType&>(value);
        rv_allocator<arrayEntry> al;
        arr.emplace_back(std::allocate_shared<ClassEntryx>(al,EntryType::int32, name, val));

        std::stringstream str;
        str << this << " Add " << name.data() << " " << val << "\n";
        OutputDebugStringA(str.str().c_str());
    }
    virtual void add_entry(const r_string &name, int64_t val) {
        auto& arr = std::any_cast<classType&>(value);
        rv_allocator<arrayEntry> al;
        arr.emplace_back(std::allocate_shared<ClassEntryx>(al, EntryType::int64, name, val));

        std::stringstream str;
        str << this << " Add " << name.data() << " " << val << "\n";
        OutputDebugStringA(str.str().c_str());
    }
    virtual void compress() {
        std::stringstream str;
        str << this << " Compact " << "\n";
        OutputDebugStringA(str.str().c_str());
    }

    //! Delete the entry. Note: it could be used in rare cases only!
    virtual void _placeholder(const r_string &name) { __debugbreak(); }
};
//#define SerializationLogging 1
class ClassEntryJson : public param_archive_entry {
    friend class ClassEntryRef;
public:
    r_string name;
    EntryType type;
    json* value;

    ClassEntryJson() {}
    ClassEntryJson(EntryType type_, r_string name_, json* val_) : name(name_), type(type_), value(val_) {}
    ClassEntryJson(EntryType type_, r_string name_) : name(name_), type(type_) {}


    //! virtual destructor
    virtual ~ClassEntryJson() {

    }

    // generic entry
    int entry_count() const override {
#if SerializationLogging
          std::stringstream str;
      str << this << " entryCount " << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

        return value->size();
    }


    param_archive_entry* get_entry_by_index(int i) const override {
#if SerializationLogging
          std::stringstream str;
        str << this << " get_entry_by_index " << i << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

        if (type == EntryType::array) {
            return new ClassEntryJson(EntryType::boolean,""sv,&(*value)[i]);
        }
        if (type == EntryType::clas) {
            __debugbreak();
        }
        __debugbreak();
        return nullptr;
    }

    r_string current_entry_name() override { return name; }
    param_archive_entry* get_entry_by_name(const r_string& name) const override {
#if SerializationLogging
          std::stringstream str;
        str << this << " get_entry_by_name " << name << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

        if (type == EntryType::clas) {
            if (value->count(name.c_str()))
                return new ClassEntryJson(EntryType::clas, name, &(*value)[name.c_str()]);
        }
        return nullptr;
    }
    operator float() const override { return *value; }
    operator int() const override { return *value; }

    operator int64_t() const override { return *value; }

    operator r_string() const override { return r_string(value->get<std::string>()); }

    operator bool() const override { return*value; }
    //GetContext
    r_string _placeholder1(uint32_t member = NULL) const override {
        return r_string();
    }

    // array
    void reserve(int count) override {
#if SerializationLogging
          std::stringstream str;
        str << this << " ReserveArrayElements " << count << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

    }
    void add_array_entry(float val) override {
        value->push_back(val);

#if SerializationLogging
          std::stringstream str;
        str << this << " AddValue " << val << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

    }

    void add_array_entry(int val) override {
        value->push_back(val);

#if SerializationLogging
          std::stringstream str;
        str << this << " AddValue " << val << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

    }

    void add_array_entry(int64_t val) override {
        value->push_back(val);

#if SerializationLogging
          std::stringstream str;
        str << this << " AddValue " << val << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

    }

    //void add_array_entry(bool val) override {
    //    std::stringstream str;
    //    str << this << " AddValue " << val << "\n";
    //    OutputDebugStringA(str.str().c_str());
    //}
    void add_array_entry(const r_string &val) override {
        value->push_back(val.c_str());


#if SerializationLogging
          std::stringstream str;
        str << this << " AddValue " << val/*.data()*/ << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

    }

    int count() const override {

#if SerializationLogging
          std::stringstream str;
        str << this << " getArrCount " << value->size() << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

        return value->size();
    }

    param_archive_array_entry* operator [](int i) const override {

#if SerializationLogging
          std::stringstream str;
        str << this << " getArrElement " << i << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

        return new JArrayEntry(&(*value)[i]);
    }

    // class


    param_archive_entry* add_entry_class(const r_string& name, bool guaranteedUnique = false) override {
        (*value)[name.c_str()] = json::object();

#if SerializationLogging
          std::stringstream str;
        str << this << " AddClass " << name.data() << " " << guaranteedUnique << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

        return new ClassEntryJson(EntryType::clas,name,&(*value)[name.c_str()]);
    };

    param_archive_entry* add_entry_array(const r_string& name) override {
        (*value)[name.c_str()] = json::array();

#if SerializationLogging
          std::stringstream str;
        str << this << " AddArray " << name.data() << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

        return new ClassEntryJson(EntryType::array, name, &(*value)[name.c_str()]);
    };

    virtual void add_entry(const r_string &name, const r_string &val) {
        (*value)[name.c_str()] = val.c_str();

#if SerializationLogging
          std::stringstream str;
        str << this << " Add " << name.data() << " " << val.data() << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

    }
    virtual void add_entry(const r_string &name, float val) {
        (*value)[name.c_str()] = val;

#if SerializationLogging
          std::stringstream str;
        str << this << " Add " << name.data() << " " << val << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

    }
    virtual void add_entry(const r_string &name, int val) {
        (*value)[name.c_str()] = val;

#if SerializationLogging
          std::stringstream str;
        str << this << " Add " << name.data() << " " << val << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

    }
    virtual void add_entry(const r_string &name, int64_t val) {
        (*value)[name.c_str()] = val;

#if SerializationLogging
          std::stringstream str;
        str << this << " Add " << name.data() << " " << val << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

    }
    virtual void compress() {
#if SerializationLogging
        std::stringstream str;
        str << this << " Compact " << "\n";
        OutputDebugStringA(str.str().c_str());
#endif

    }

    //! Delete the entry. Note: it could be used in rare cases only!
    virtual void _placeholder(const r_string &name) { __debugbreak(); }
};


ClassEntryRef::ClassEntryRef(std::shared_ptr<ClassEntryx> v) : value(v.get()) {}
ClassEntryRef::ClassEntryRef(std::shared_ptr<ClassEntryJson> v) : value(v.get()) {}


int intercept::api_version() {
    return 1;
}

std::vector<std::shared_ptr<zmsg>> pvarQueue;
std::mutex pvarMutex;

intercept::network::client::client* pClient = nullptr;
intercept::network::server::router* pRouter = nullptr;
std::unique_ptr<std::ofstream> logF;

void  intercept::on_frame() {  
    if (!pClient || !logF) return;
    if (pvarQueue.empty()) return;
    pvarMutex.lock();
    for (auto& it : pvarQueue) {
        std::string dataStr = it->pop_front();
        json data = json::parse(dataStr);

        uint32_t cmd = data["cmd"];
        if (cmd == 0) {//setVar
            
            std::string name = data["name"];
            json valueBase = data["value"];

            *logF << "set " << dataStr.length() << " " << name <<"\n";

            json base;

            param_archive ar(rv_allocator<ClassEntryJson>::create_single(EntryType::clas, ""sv, &base));

            game_value out;
            ar._isExporting = false;
            ar._p3 = 1;
            (&out)->serialize(ar);
            ar._p3 = 2;
            (&out)->serialize(ar);

            //#TODO support for other namespaces

            sqf::set_variable(sqf::mission_namespace(), name, out);
        }



    }
    pvarMutex.unlock();
}


void intercept::pre_start() {
    //if (sqf::is_server()) {  //We need to host a Server
    //    if (!network::server::GRouter) network::server::GRouter = new intercept::network::server::router();
    //    network::server::GRouter->bind("tcp://0.0.0.0:5555");  //#TODO correct port
    //    std::thread([]() { network::server::GRouter->route(); }).detach();
    //}

    static auto _atanh = intercept::client::host::registerFunction("seriaTest"sv, ""sv, [](uintptr_t, game_value_parameter right) -> game_value {
        auto ncnst = const_cast<game_value&>(right);
        json base;
 
        param_archive ar(rv_allocator<ClassEntryJson>::create_single(EntryType::clas, ""sv, &base));


        (&ncnst)->serialize(ar);

        auto dmp = base.dump(-1);
        OutputDebugString(dmp.c_str());
        auto cbor = json::to_cbor(base);
        std::string cborStr((char*)cbor.data(), cbor.size());




        //std::string compressed_j = gzip::compress(dmp.data(), dmp.size(), Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY);
        //std::string compressed_c = gzip::compress((char*)cbor.data(), cbor.size(), Z_BEST_COMPRESSION, Z_DEFAULT_STRATEGY);


        game_value out;
        ar._isExporting = false;
        ar._p3 = 1;
        (&out)->serialize(ar);
        ar._p3 = 2;
        (&out)->serialize(ar);
        
        
        return out;
    }, GameDataType::ANY, GameDataType::ANY);

    static auto _pvar = intercept::client::host::registerFunction("publicVariable"sv, ""sv, [](uintptr_t, game_value_parameter right) -> game_value {
        if (!pClient) {
            sqf::public_variable(right);
            return {};
        }

        auto var = sqf::get_variable(sqf::current_namespace(), right);
        json base;
        
        param_archive ar(rv_allocator<ClassEntryJson>::create_single(EntryType::clas, ""sv, &base));


        (&var)->serialize(ar);

        //auto dmp = base.dump(1);


        std::string varname = right;
        sqf::system_chat(varname);
        //if (dmp.find("Enables a bullet trace") != std::string::npos) {
        //    __debugbreak();
        //}

        //   OutputDebugString(varname.c_str());
        //   OutputDebugString(dmp.c_str());

        //auto cbor = json::to_cbor(base);
        //std::string cborStr((char*) cbor.data(), cbor.size());


        nlohmann::json info;
        info["name"] = varname;
        info["value"] = base; 
        info["cmd"] = 0;


        auto msg = std::make_shared<zmsg>();
        msg->push_front(info.dump(-1));
        *logF << "send " << msg->peek_front().length() << " "<< varname << "\n";
        logF->flush();
        pClient->send(serviceType::pvar, msg, false);

        return {};
    }, GameDataType::NOTHING, GameDataType::STRING);

    static auto _pvarC = intercept::client::host::registerFunction("publicVariableClient"sv, ""sv, [](uintptr_t, game_value_parameter left, game_value_parameter right) -> game_value {
        sqf::public_variable_client(left, right);
        if (!pClient) {
            return {};
        }
        
        auto var = sqf::get_variable(sqf::current_namespace(), right);
        json base;

        param_archive ar(rv_allocator<ClassEntryJson>::create_single(EntryType::clas, ""sv, &base));


        (&var)->serialize(ar);

        //auto dmp = base.dump(1);


        std::string varname = right;
        sqf::system_chat(varname);
        //if (dmp.find("Enables a bullet trace") != std::string::npos) {
        //    __debugbreak();
        //}

        //   OutputDebugString(varname.c_str());
        //   OutputDebugString(dmp.c_str());

        //auto cbor = json::to_cbor(base);
        //std::string cborStr((char*) cbor.data(), cbor.size());


        nlohmann::json info;
        info["name"] = varname;
        info["value"] = base;
        info["clientID"] = (int)left;
        info["cmd"] = 0;


        auto msg = std::make_shared<zmsg>();
        msg->push_front(info.dump(-1));
        *logF << "sendc " << msg->peek_front().length() << " " << varname << "\n";
        logF->flush();
        pClient->send(serviceType::pvar, msg, false);

        return {};
    }, GameDataType::NOTHING, GameDataType::SCALAR, GameDataType::STRING);



}

void  intercept::pre_init() {
    //if (!sqf::is_server()) {
    #ifndef __linux__
        //if (pClient) __debugbreak();
    #endif
        pClient = new intercept::network::client::client("tcp://127.0.0.1:5555",  sqf::client_owner());
        logF = std::make_unique<std::ofstream>("P:/" + std::to_string(sqf::client_owner()));

        pClient->asynchronousRequestHandler = [](std::shared_ptr<zmsg> msg) {

            std::cout << "message\n";
        };

        pClient->serviceHandlers[serviceType::pvar] = [](std::shared_ptr<zmsg> msg) {
            pvarMutex.lock();
            pvarQueue.push_back(msg);
            pvarMutex.unlock();
            std::cout << "pvar\n";
        };



        pClient->synchronousRequestHandler = [](std::shared_ptr<zmsg> msg) {
            std::cout << "request\n";
            msg->clear();
            msg->push_front("Hello answer!");
        };







    //}
}

void intercept::post_init() {

}

void intercept::mission_ended() {
    //Server never closes socket..
    //#TODO we might tell clients to leave though.. But they should already leave by themselves
    //GRouter.stop();
    if (pClient) delete pClient; //#TODO we probably need to disconnect first ^^
    pClient = nullptr;
}
