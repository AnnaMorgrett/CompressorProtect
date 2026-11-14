#ifndef EEPROM_h
#define EEPROM_h

#include <Arduino.h>

#define CR_OPTPG_Set ((uint32_t)0x00000010)
#define CR_OPTPG_Reset ((uint32_t)0xFFFFFFEF)
#define CR_OPTER_Set ((uint32_t)0x00000020)
#define CR_OPTER_Reset ((uint32_t)0xFFFFFFDF)
#define CR_STRT_Set ((uint32_t)0x00000040)
#define CR_LOCK_Set ((uint32_t)0x00000080)
#define FLASH_KEY1 ((uint32_t)0x45670123)
#define FLASH_KEY2 ((uint32_t)0xCDEF89AB)

class EEPROMClass
{
public:
    EEPROMClass(void);
    ~EEPROMClass(void);
    void begin(void);
    uint8_t *getDataPtr();
    uint8_t const *getConstDataPtr() const;
    uint32_t ReadOptionBytes(void);
    uint8_t read(int const idx);
    void write(int const idx, uint8_t const val);
    void update(int const idx, uint8_t const val)
    {
        if (read(idx) != val)
        {
            write(idx, val);
            commit();
        }
    }
    void erase(void);
    bool commit(void);
    bool end(void);
    template <typename T>
    T &get(int const address, T &t)
    {
        if (address < 0 || (size_t)address + sizeof(T) > _size)
            return t;
        memcpy((uint8_t *)&t, _data + address, sizeof(T));
        return t;
    }
    template <typename T>
    const T &put(int const address, const T &t)
    {
        if (address < 0 || (size_t)address + sizeof(T) > _size)
            return t;
        if (memcmp(_data + address, (const uint8_t *)&t, sizeof(T)) != 0)
        {
            _dirty = true;
            memcpy(_data + address, (const uint8_t *)&t, sizeof(T));
        }
        return t;
    }
    size_t length() { return _size; }
    uint8_t &operator[](int const address) { return getDataPtr()[address]; }
    uint8_t const &operator[](int const address) const { return getConstDataPtr()[address]; }

protected:
    uint8_t _data[26] = {0};
    bool _dirty = false;
    size_t _size = 0;
};

extern EEPROMClass EEPROM;

#endif