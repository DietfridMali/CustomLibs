#include <map>
#include <initializer_list>
#include <utility>

template <typename KEY_T, typename DATA_T>
class StdMap
{
private:
    std::map<KEY_T, DATA_T> m_map;

public:
    StdMap(int /*capacity*/ = 0) {}
    ~StdMap() = default;

    using tComparator = int(*)(void* context, const KEY_T& k1, const KEY_T& k2);
    using iterator = typename std::map<KEY_T, DATA_T>::iterator;
    using const_iterator = typename std::map<KEY_T, DATA_T>::const_iterator;

    iterator begin() { return m_map.begin(); }
    iterator end() { return m_map.end(); }

    const_iterator begin() const { return m_map.begin(); }
    const_iterator end()   const { return m_map.end(); }

    const_iterator cbegin() const { return m_map.cbegin(); }
    const_iterator cend()   const { return m_map.cend(); }

    inline int Size() const {
        return static_cast<int>(m_map.size());
    }

    DATA_T* Find(const KEY_T& key) {
        auto it = m_map.find(key);
        return (it != m_map.end()) ? &it->second : nullptr;
    }
    // const
    const DATA_T* Find(const KEY_T& key) const {
        auto it = m_map.find(key);
        return (it != m_map.end()) ? &it->second : nullptr;
    }
    // rvalue-Key delegiert
    DATA_T* Find(KEY_T&& key) { return Find(static_cast<const KEY_T&>(key)); }

    template <typename Predicate>
    DATA_T* FindIf(Predicate pred) {
        for (auto& [k, v] : m_map) {
            if (pred(k))
                return &v;
        }
        return nullptr;
    }

    bool Find(const KEY_T& key, DATA_T& value) {
        auto it = m_map.find(key);
        if (it == m_map.end())
            return false;
        value = it->second;
        return true;
    }

    typename std::map<KEY_T, DATA_T>::iterator FindData(const DATA_T& data) {
        for (auto it = m_map.begin(); it != m_map.end(); ++it) {
            if (it->second == data)
                return it;
        }
        return m_map.end();
    }

    bool Extract(const KEY_T& key, DATA_T& data) {
        auto it = m_map.find(key);
        if (it == m_map.end())
            return false;
        data = std::move(it->second);
        m_map.erase(it);
        return true;
    }

    inline bool Extract(KEY_T&& key, DATA_T& data) {
        return Extract(static_cast<const KEY_T&>(key), data);
    }

    // Fügt nur ein, wenn der Key noch nicht existiert
    template<typename K = KEY_T, typename D = DATA_T>
    bool Insert(K&& key, D&& data) {
        return m_map.emplace(std::forward<K>(key), std::forward<D>(data)).second;
    }

    template<typename K = KEY_T>
    bool Remove(K&& key) {
        return m_map.erase(std::forward<K>(key)) > 0;
    }

    void Destroy() {
        m_map.clear();
    }

    template<typename Func>
    bool Walk(Func processNode) {
        for (auto& [key, data] : m_map) {
            if (!processNode(key, data))
                return false;
        }
        return true;
    }

    DATA_T* Min() {
        if (m_map.empty())
            return nullptr;
        return &(m_map.begin()->second);
    }

    DATA_T* Max() {
        if (m_map.empty())
            return nullptr;
        auto it = m_map.end();
        --it;
        return &(it->second);
    }

    bool ExtractMin(DATA_T& data) {
        if (m_map.empty())
            return false;
        auto it = m_map.begin();
        data = std::move(it->second);
        m_map.erase(it);
        return true;
    }

    bool ExtractMax(DATA_T& data) {
        if (m_map.empty())
            return false;
        auto it = m_map.end();
        --it;
        data = std::move(it->second);
        m_map.erase(it);
        return true;
    }

    // Update: Ersetzt vorhandene Daten oder fügt ein neues Element ein (immer true)
    bool Update(const KEY_T& key, const DATA_T& data) {
        m_map[key] = data;
        return true;
    }

    template<typename K = KEY_T>
    inline DATA_T& operator[] (K&& key) {
        return m_map[std::forward<K>(key)];
    }

    inline StdMap& operator= (std::initializer_list<std::pair<KEY_T, DATA_T>> data) {
        for (auto& d : data)
            m_map[d.first] = d.second;
        return *this;
    }

    StdMap(const StdMap& other) = default;
    StdMap& operator=(const StdMap& other) = default;

    StdMap& operator+=(const StdMap& other) {
        m_map.insert(other.m_map.begin(), other.m_map.end());
        return *this;
    }

    inline StdMap& Copy(const StdMap& other) {
        m_map = other.m_map;
        return *this;
    }
};
