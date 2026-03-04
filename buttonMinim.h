// улучшенная версия для работы с кнопкой, версия 2.0

#ifndef buttonMinim_h
#define buttonMinim_h

#include <Arduino.h>

class buttonMinim {
  public:
    buttonMinim(uint8_t pin);
    void tick();                    // теперь tick() нужно вызывать в loop
    boolean pressed();
    boolean clicked();
    boolean holding();
    boolean holded();
    
  private:
    uint8_t _pin;
    uint32_t _btnTimer;
    bool _lastState;
    bool _btnState;
    
    // Флаги событий
    bool _pressFlag;
    bool _clickFlag;
    bool _holdFlag;
    bool _holdedFlag;
    bool _holdingFlag;
};

buttonMinim::buttonMinim(uint8_t pin) {
  _pin = pin;
  pinMode(_pin, INPUT_PULLUP);
  _lastState = HIGH;
  _btnState = HIGH;
  _btnTimer = 0;
  _pressFlag = false;
  _clickFlag = false;
  _holdFlag = false;
  _holdedFlag = false;
  _holdingFlag = false;
}

void buttonMinim::tick() {
  // Читаем состояние кнопки с антидребезгом
  bool currentState = digitalRead(_pin);
  
  // Если состояние изменилось
  if (currentState != _lastState) {
    _lastState = currentState;
    
    // Если кнопка нажата (LOW при INPUT_PULLUP)
    if (currentState == LOW) {
      _btnTimer = millis();
      _pressFlag = true;
      _holdedFlag = true;  // для holded()
      _holdingFlag = false;
    } 
    // Если кнопка отпущена
    else {
      // Проверяем, было ли это короткое нажатие
      if (millis() - _btnTimer < 500) {  // 500мс - порог удержания
        _clickFlag = true;
        _holdFlag = false;
        _holdingFlag = false;
      }
      _holdedFlag = false;
    }
  }
  
  // Обработка удержания
  if (_lastState == LOW && millis() - _btnTimer > 500) {
    if (!_holdingFlag) {
      _holdingFlag = true;
      _holdFlag = true;
    }
  }
  
  _btnState = _lastState;
}

boolean buttonMinim::pressed() {
  if (_pressFlag) {
    _pressFlag = false;
    return true;
  }
  return false;
}

boolean buttonMinim::clicked() {
  if (_clickFlag) {
    _clickFlag = false;
    return true;
  }
  return false;
}

boolean buttonMinim::holding() {
  return _holdingFlag;
}

boolean buttonMinim::holded() {
  if (_holdFlag && _holdedFlag) {
    _holdFlag = false;
    return true;
  }
  return false;
}

#endif