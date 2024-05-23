#include <EEPROM.h>
#include "memory.h"
#include <Arduino.h>

Memory::Memory() {
    EEPROM.begin(1000);
    _ready = !((bool) EEPROM.read(0));
}

Memory::~Memory() {
    EEPROM.end();
}

const bool Memory::ready() const {
    return _ready;
}

void Memory::ready(const bool value) {
    EEPROM.put(0, !value);
}

char* Memory::readNext() {
    char* result = new char[PROPERTY_VALUE_LENGTH];

    for (unsigned int i = 0; i < PROPERTY_VALUE_LENGTH; i++) {
        char current = (char) EEPROM.read(_index++);

        if (current == DELIMITER) {
            return result;
        } else {
            result[i] = current;
        }
    }

    return result;
}

void Memory::write(const char* value) {
    for (unsigned int i = 0; i < strlen(value); i++) {
        EEPROM.put(_index++, value[i]);
    }

    EEPROM.put(_index++, DELIMITER);
}

void Memory::commit() {
    EEPROM.commit();
    _index = 1;
}

void Memory::clear() {
    for (unsigned int i = 0; i < EEPROM.length(); i++) {
        EEPROM.put(i, 255);
    }
}