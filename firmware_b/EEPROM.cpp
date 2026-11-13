#include <EEPROM.h>

#define OB_AVAIL_DATA_START 8

EEPROMClass::EEPROMClass(void) {}
EEPROMClass::~EEPROMClass(void) { end(); }

uint32_t EEPROMClass::ReadOptionBytes() {
  uint32_t uRes = 0;
  uRes |= OB->Data0; uRes <<= 16;
  uRes |= OB->Data1;
  return uRes;
}

void EEPROMClass::begin(void) {
  _size = 26;
  uint16_t *ob16p = (uint16_t *)OB_BASE;
  _data[0] = (uint8_t)ob16p[2];
  _data[1] = (uint8_t)ob16p[3];
  for (int i = 2; i < _size; i++) _data[i] = (uint8_t)ob16p[OB_AVAIL_DATA_START + (i - 2)];
  _dirty = false;
}

uint8_t * EEPROMClass::getDataPtr() { _dirty = true; return &_data[0]; }
uint8_t const * EEPROMClass::getConstDataPtr() const { return &_data[0]; }

uint8_t EEPROMClass::read(int const idx) {
  if (_data && idx >= 0 && (size_t)idx < _size) return _data[idx];
  return 0;
}

void EEPROMClass::write(int const idx, uint8_t const val) {
  if (_data && idx >= 0 && (size_t)idx < _size) { _dirty = true; _data[idx] = val; }
}

void EEPROMClass::erase(void) {
  _dirty = true;
  for (int i = 0; i < _size; i++) _data[i] = 0xFF;
}

bool EEPROMClass::commit() {
  if (!_dirty) return true;

  volatile uint16_t hold[8];
  uint32_t *hold32p = (uint32_t *)hold;
  uint32_t *ob32p   = (uint32_t *)OB_BASE;
  hold32p[0] = ob32p[0];
  hold32p[1] = _data[0] + (_data[1] << 16);
  hold32p[2] = ob32p[2];
  hold32p[3] = ob32p[3];

  FLASH->KEYR   = FLASH_KEY1;
  FLASH->KEYR   = FLASH_KEY2;
  FLASH->OBKEYR = FLASH_KEY1;
  FLASH->OBKEYR = FLASH_KEY2;

  FLASH->CTLR |= CR_OPTER_Set;
  FLASH->CTLR |= CR_STRT_Set;
  while (FLASH->STATR & FLASH_BUSY);
  FLASH->CTLR &= CR_OPTER_Reset;

  FLASH->CTLR |= CR_OPTPG_Set;
  uint16_t *ob16p = (uint16_t *)OB_BASE;
  for (int i = 0; i < sizeof(hold) / sizeof(hold[0]); i++) {
    ob16p[i] = hold[i];
    while (FLASH->STATR & FLASH_BUSY);
  }

  if (_data && _size) {
    uint16_t *ob16p2 = (uint16_t *)OB_BASE;
    for (int i = 2; i < _size; i++) {
      if (_data[i] != 0xFF) {
        ob16p2[OB_AVAIL_DATA_START + (i - 2)] = _data[i];
        while (FLASH->STATR & FLASH_BUSY);
      }
    }
  }

  FLASH->CTLR &= CR_OPTPG_Reset;
  FLASH->CTLR |= CR_LOCK_Set;
  return true;
}

bool EEPROMClass::end() {
  bool retval = commit();
  _size = 0;
  return retval;
}

EEPROMClass EEPROM;