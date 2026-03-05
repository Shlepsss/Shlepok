 /*
  Шпрот (шлепок ОС)
  охуеть не встать

  На атмеге328п-пу
  вау как круто
  создатель (додик) - дипсик
  режиссёр - @shlepsss (тоже додик)
*/

// ================ НАСТРОЙКИ ================
// Пины
#define BTN_UP 5      // Кнопка UP (ввод цифры/листание вверх)
#define BTN_DWN 6     // Кнопка DOWN (выбор цифры/листание вниз)
#define BTN_SET 7     // Кнопка SET (выбор/подтверждение)

// Таймауты энергосбережения
#define DIMM_TIMEOUT 120000UL   // 2 минуты до затемнения
#define OFF_TIMEOUT  240000UL   // 4 минут до выключения

// Режимы главного меню
#define MODE_MENU 0
#define MODE_FILES 1
#define MODE_CALC 2

// ================ БИБЛИОТЕКИ ================
#include "buttonMinim.h"
#include <SPI.h>
#include <SD_fix.h>
#include <GyverOLED.h>

// ================ ОБЪЕКТЫ ================
buttonMinim buttUP(BTN_UP);
buttonMinim buttSET(BTN_SET);
buttonMinim buttDWN(BTN_DWN);
GyverOLED oled(0x3C);
Sd2Card card;
SdVolume volume;
SdFile root;
File currentFile;

// ================ ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ================
// Режимы и навигация
byte currentMode = MODE_MENU;
byte menuItem = 0;
const char* menuItems[] = {"SD читaлкa", "Kaлькyлятop"};
const byte menuItemsCount = 2;

// Для файлового менеджера
String filenames = "";          // временно, только для показа списка
String selectedFileName = "";   // имя выбранного файла
int8_t filesAmount = 0;
int8_t selectedFileIndex = 0;
uint32_t btnTimer;
bool lypaFlag = false;

struct Calculator {
  enum State { INPUT_FIRST, INPUT_SECOND, RESULT };
  State state = INPUT_FIRST;

  static constexpr int32_t SCALE = 1000;

  int32_t first = 0;
  int32_t second = 0;
  int32_t result = 0;
  char op = 0;

  int32_t current = 0;
  bool negative = false;
  bool decimalMode = false;
  uint8_t decimalPlaces = 0;
  bool dirty = true;

  bool opSelectionMode = false;
  uint8_t selectedOpIndex = 0;

  void clear() {
    state = INPUT_FIRST;
    first = second = result = 0;
    op = 0;
    current = 0;
    negative = false;
    decimalMode = false;
    decimalPlaces = 0;
    opSelectionMode = false;
    dirty = true;
  }

  void addDigit(uint8_t digit) {
    if (state == RESULT) clear();
    if (!decimalMode) {
      if (current < (INT32_MAX - digit) / 10) {
        current = current * 10 + digit;
      }
    } else {
      if (decimalPlaces < 3) {
        current = current * 10 + digit;
        decimalPlaces++;
      }
    }
    dirty = true;
  }

  void setDecimal() {
    if (state == RESULT) clear();
    if (!decimalMode) {
      decimalMode = true;
      decimalPlaces = 0;
    }
    dirty = true;
  }

  void toggleSign() {
    negative = !negative;
    dirty = true;
  }

  int32_t getCurrent() {
    int32_t val = current;
    if (negative) val = -val;
    return val;
  }

  void setOp(char operation) {
    if (state == INPUT_FIRST) {
      first = getCurrent() * SCALE;
      op = operation;
      state = INPUT_SECOND;
      current = 0;
      negative = false;
      decimalMode = false;
      decimalPlaces = 0;
      opSelectionMode = false;
      dirty = true;
    }
  }

  void calculate() {
    if (state == INPUT_SECOND) {
      second = getCurrent() * SCALE;
      switch (op) {
        case '+': result = first + second; break;
        case '-': result = first - second; break;
        case '*': result = (int32_t)(((int64_t)first * second) / SCALE); break;
        case '/':
          if (second != 0) result = (int32_t)(((int64_t)first * SCALE) / second);
          else result = 0;
          break;
      }
      state = RESULT;
      current = result / SCALE;
      negative = (result < 0);
      decimalMode = false;
      decimalPlaces = 0;
      dirty = true;
    }
  }

  void getDisplayString(char* buf, size_t len) {
    if (state == RESULT) {
      int32_t intPart = result / SCALE;
      int32_t fracPart = result % SCALE;
      if (fracPart < 0) fracPart = -fracPart;
      snprintf(buf, len, "%ld.%03ld", intPart, fracPart);
    } else {
      if (current == 0 && !decimalMode) {
        strcpy(buf, "0");
      } else {
        int32_t intPart = current;
        if (decimalMode) {
          int32_t divisor = 1;
          for (int i = 0; i < decimalPlaces; i++) divisor *= 10;
          intPart = current / divisor;
          int32_t fracPart = current % divisor;
          if (negative) intPart = -intPart;
          char fmt[10];
          snprintf(fmt, sizeof(fmt), "%%ld.%%0%dld", decimalPlaces);
          snprintf(buf, len, fmt, intPart, fracPart);
        } else {
          if (negative) intPart = -intPart;
          snprintf(buf, len, "%ld", intPart);
        }
      }
    }
  }
} calc;

// Для управления контрастом и энергосбережением
uint8_t currentContrast = 255;
uint32_t lastActivity = 0;
bool isDimmed = false;
bool isOff = false;
bool sdActive = false;          // флаг активности SD

// ================ ПРОТОТИПЫ (ВСЁ ДЛЯ ЭКРАНА - СВЕРХУ) ================
void initOLED();
void drawMainMenu();
void listFiles();
void readFilePage();
void showSDInfo();
void setContrast(uint8_t value);
void oledSetPower(bool on);
void checkActivity();
void handlePowerSave();
int freeRam();

// ================ ПРОТОТИПЫ (ЛОГИКА - СНИЗУ) ================
void initSD();
void sdStart();
void sdEnd();
void sdOperation(void (*func)());
void fileManagerLoop();
void calculatorLoop();
void openFile();
void nextFile();
void prevFile();
void nextPage();
void prevPage();
void returnToFileManager();

// ================ ФУНКЦИЯ ПОЛУЧЕНИЯ СВОБОДНОЙ RAM ================
int freeRam() {
  extern int __heap_start, *__brkval;
  int v;
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval);
}

// ================ УПРАВЛЕНИЕ КОНТРАСТОМ ================
void setContrast(uint8_t value) {
  if (!isOff && currentContrast != value) {
    oled.setContrast(value);
    currentContrast = value;
  }
}

// ================ УПРАВЛЕНИЕ ПИТАНИЕМ ================
void oledSetPower(bool on) {
  if (on) {
    oled.setPower(true);
    isOff = false;
  } else {
    oled.setPower(false);
    isOff = true;
  }
}

// ================ УПРАВЛЕНИЕ АКТИВНОСТЬЮ ================
void checkActivity() {
  lastActivity = millis();
  
  if (isDimmed) {
    isDimmed = false;
    if (!sdActive) setContrast(255);
  }
  
  if (isOff) {
    oledSetPower(true);
    if (!sdActive) setContrast(255);
    switch (currentMode) {
      case MODE_MENU: drawMainMenu(); break;
      case MODE_FILES: listFiles(); break;
      case MODE_CALC: calc.dirty = true; break;
    }
  }
}

// ================ ЭНЕРГОСБЕРЕЖЕНИЕ ================
void handlePowerSave() {
  uint32_t now = millis();
  uint32_t inactive = now - lastActivity;
  
  if (sdActive) return;
  
  if (!isOff && !isDimmed && inactive > DIMM_TIMEOUT) {
    setContrast(32);
    isDimmed = true;
  } else if (!isOff && inactive > OFF_TIMEOUT) {
    oledSetPower(false);
  }
}

// ================ УПРАВЛЕНИЕ SD ОПЕРАЦИЯМИ ================
void sdStart() {
  sdActive = true;
  setContrast(1);
}

void sdEnd() {
  sdActive = false;
  if (!isDimmed && !isOff) {
    setContrast(255);
  } else if (isDimmed && !isOff) {
    setContrast(32);
  }
}

void sdOperation(void (*func)()) {
  sdStart();
  func();
  sdEnd();
}

// ================ ИНИЦИАЛИЗАЦИЯ OLED ================
void initOLED() {
  Wire.begin();
  Wire.setClock(400000L);

  #define OLED_SOFT_BUFFER_64
  oled.init(OLED128x64);
  setContrast(255);
  oled.clear();
  oled.home();

  oled.update();
}

// ================ ГЛАВНОЕ МЕНЮ ================
void drawMainMenu() {
  oled.clear();
  oled.home();

  oled.scale2X();
  oled.println(" Шлeпoк");
  oled.scale1X();
  oled.println("==========");
  oled.println();

  for (byte i = 0; i < menuItemsCount; i++) {
    if (i == menuItem) {
      oled.inverse(true);
      oled.print("> ");
    } else {
      oled.inverse(false);
      oled.print("  ");
    }
    oled.println(menuItems[i]);
  }
  oled.inverse(false);
  
  oled.setCursor(0, 7);
  oled.print("O3У: ");
  oled.print(freeRam());
  oled.print(" B");
  
  oled.update();
}

// ================ ИНФОРМАЦИЯ О SD КАРТЕ ================
void showSDInfo() {
  oled.clear();
  oled.home();
  oled.scale1X();

  if (filesAmount > 0) {
    oled.println("Tип SD Kapты:");
    switch (card.type()) {
      case SD_CARD_TYPE_SD1: oled.println("SD1"); break;
      case SD_CARD_TYPE_SD2: oled.println("SD2"); break;
      case SD_CARD_TYPE_SDHC: oled.println("SDHC"); break;
      default: oled.println("Xepня");
    }

    oled.setCursor(0, 4);
    oled.print("Paзмep: ");
    uint32_t size = card.cardSize();
    size *= 512;
    size /= 1048576;
    oled.print(size);
    oled.println(" MB");

    oled.setCursor(0, 6);
    oled.print("Фaйлoв: ");
    oled.print(filesAmount);
  } else {
    oled.print("Heт SD/фaйлoв");
  }

  oled.update();
}

// ================ ОТОБРАЖЕНИЕ СПИСКА ФАЙЛОВ ================
void listFiles() {
  oled.clear();
  oled.home();

  if (filesAmount == 0) {
    oled.print("No files found");
    oled.update();
    return;
  }

  int i = 0;
  int pos = 0;
  byte page = selectedFileIndex / 8;
  selectedFileName = "";

  while (true) {
    if (pos == selectedFileIndex) {
      oled.inverse(true);
      int start = i;
      while (filenames[i] != '\n' && filenames[i] != 0) i++;
      selectedFileName = filenames.substring(start, i);
      i = start;
    } else {
      oled.inverse(false);
    }

    if ((pos / 8) >= page) {
      while (filenames[i] != '\n' && filenames[i] != 0) {
        oled.print(filenames[i]);
        i++;
      }
      if (filenames[i] == '\n') oled.println();
    }

    if (filenames[i] == '\n') {
      pos++;
      i++;
      if (pos > filesAmount) break;
    }
    if ((pos / 8) == (page + 1)) break;
    if (filenames[i] == 0) break;
  }
  oled.inverse(false);
  
  oled.setCursor(0, 7);
  oled.print("RAM:");
  oled.print(freeRam());
  oled.print("B ");
  oled.print(filesAmount);
  oled.print(" files");
  
  oled.update();
}

// ================ ЧТЕНИЕ СТРАНИЦЫ ФАЙЛА ================
void readFilePage() {
  if (!currentFile) return;

  oled.clear();
  oled.home();
  if(lypaFlag) {oled.scale1X();}
  else {oled.scale2X();}

  // Читаем и выводим пока не заполнится экран или не конец файла
  while (!oled.isEnd()) {
    int c = currentFile.read();
    if (c == -1) break; // Конец файла
    
    if (c == '\n') {
      oled.println();
    } else if (c >= 32) { // Печатаемые символы
      oled.print((char)c);
    }
    // Остальные символы игнорируем
  }
  
  oled.update();
}

// ================ SETUP ================
void setup() {
  initOLED();
  oled.scale2X();
  oled.println("  Shlepok");
  oled.scale1X();
  oled.println();
  oled.println("Made in Russia");
  oled.scale2X();
  oled.println();
  oled.println("@shlepsss");
  oled.println();
  oled.scale1X();
  oled.update();
  delay(1000);

  initSD();
  showSDInfo();
  delay(1000);
  drawMainMenu();
  checkActivity();
}

// ================ LOOP ================
void loop() {
  // ВАЖНО: вызываем tick() для всех кнопок в самом начале loop
  buttUP.tick();
  buttSET.tick();
  buttDWN.tick();

  // Проверяем активность (теперь используем флаги из tick)
  if (buttUP.pressed() || buttSET.pressed() || buttDWN.pressed() ||
      buttUP.holding() || buttSET.holding() || buttDWN.holding()) {
    checkActivity();
  }

  handlePowerSave();
  if (isOff) return;

  // Обработка режимов (используем clicked() и holded() как обычно)
  switch (currentMode) {
    case MODE_MENU:
      if (buttUP.clicked()) {
        if (--menuItem > menuItemsCount) menuItem = menuItemsCount - 1;
        drawMainMenu();
      }
      if (buttDWN.clicked()) {
        if (++menuItem >= menuItemsCount) menuItem = 0;
        drawMainMenu();
      }
      if (buttSET.clicked()) {
        currentMode = menuItem + 1;
        oled.clear();
        oled.home();
        if (currentMode == MODE_FILES) {
          sdOperation([]() {
            initSD();
            listFiles();
          });
        }
      }
      break;

    case MODE_FILES:
      fileManagerLoop();
      break;

    case MODE_CALC:
      calculatorLoop();
      break;
  }
}

// ================ ИНИЦИАЛИЗАЦИЯ SD ================
void initSD() {
  oled.print("Init SD...");
  oled.update();

  if (!card.init(SPI_HALF_SPEED, 9)) {
    oled.println(" FAIL");
    oled.println("Check card!");
    oled.update();
    filesAmount = 0;
    delay(2000);
    return;
  }
  oled.println(" OK");
  oled.update();

  if (!volume.init(card)) {
    oled.println("No FAT!");
    oled.update();
    filesAmount = 0;
    delay(2000);
    return;
  }

  root.openRoot(volume);

  filenames = "";
  root.rewind();

  char name[13];
  dir_t dir;
  filesAmount = 0;

  while (root.readDir(&dir) > 0) {
    if (dir.name[0] == DIR_NAME_FREE) break;
    if (dir.name[0] == DIR_NAME_DELETED || dir.name[0] == '.') continue;
    if (!DIR_IS_FILE_OR_SUBDIR(&dir)) continue;

    SdFile::dirName(dir, name);

    bool validName = true;
    for (int i = 0; name[i] != 0; i++) {
      if (name[i] < 32 || name[i] > 126) {
        validName = false;
        break;
      }
    }

    if (validName) {
      // Только файлы, папки игнорируем
      if (!DIR_IS_SUBDIR(&dir)) {
        filenames += name;
        filenames += '\n';
        filesAmount++;
      }
    }
  }

  SD.begin(9);
}

// ================ ВОЗВРАТ В ФАЙЛОВЫЙ МЕНЕДЖЕР ================
void returnToFileManager() {
  sdStart();
  oled.scale1X();
  if (currentFile) {
    currentFile.close();
    currentFile = File();
  }
  
  filenames = "";
  selectedFileName = "";
  
  initSD();
  
  currentMode = MODE_FILES;
  oled.clear();
  oled.home();
  listFiles();
  oled.update();
  
  sdEnd();
}

// ================ ФАЙЛОВЫЙ МЕНЕДЖЕР ================
void fileManagerLoop() {
  static uint32_t lastNavTime = 0;
  
  // Если файл открыт - режим чтения
  if (currentFile) {
    // Листание страниц вниз (дальше)
    if (buttDWN.clicked()) {
      sdStart();
      nextPage();
      sdEnd();
      checkActivity();
    }
    
    // Листание страниц вверх (назад)
    if (buttUP.clicked()) {
      sdStart();
      prevPage();
      sdEnd();
      checkActivity();
    }

    // Нажатие SET - лупа
    if (buttSET.clicked()) {
      lypaFlag = lypaFlag ? false : true;
      prevPage(100);
      checkActivity();
    }

    // Удержание SET - выход в файловый менеджер
    if (buttSET.holded()) {
      returnToFileManager();
    }
    return;
  }

  // ========== РЕЖИМ НАВИГАЦИИ ПО СПИСКУ ФАЙЛОВ ==========
  
  // Обработка кликов по DOWN (следующий файл)
  if (buttDWN.clicked()) {
    if (millis() - lastNavTime > 200) {
      lastNavTime = millis();
      sdStart();
      if (selectedFileIndex < filesAmount - 1) {
        selectedFileIndex++;
      }
      listFiles();
      sdEnd();
    }
    checkActivity();
  }
  
  // Обработка кликов по UP (предыдущий файл)
  if (buttUP.clicked()) {
    if (millis() - lastNavTime > 200) {
      lastNavTime = millis();
      sdStart();
      if (selectedFileIndex > 0) {
        selectedFileIndex--;
      }
      listFiles();
      sdEnd();
    }
    checkActivity();
  }

  // Короткое нажатие SET - открыть выбранный файл
  if (buttSET.clicked()) {
    if (selectedFileName.length() > 0 && filesAmount > 0) {
      filenames = "";
      sdStart();
      openFile();
      // Не вызываем sdEnd() здесь, так как openFile() сама управляет питанием
    }
    checkActivity();
  }

  // Удержание SET - выход в главное меню
  if (buttSET.holded()) {
    sdStart();
    if (currentFile) {
      currentFile.close();
      currentFile = File();
    }
    filenames = "";
    selectedFileName = "";
    currentMode = MODE_MENU;
    drawMainMenu();
    sdEnd();
  }
}

// ================ НАВИГАЦИЯ ПО ФАЙЛАМ ================
void nextPage() {
  if (!currentFile) return;

  int c = currentFile.read();
  if (c == -1) return; // Конец файла

  readFilePage();
}

void prevPage() {
  if (!currentFile) return;
  
  // Отматываем назад на 500 байт (примерно страница)
  uint32_t pos = currentFile.position();
  if (pos > 500) {
    currentFile.seek(pos - 500);
  } else {
    currentFile.seek(0);
  }
  readFilePage();
}

void prevPage(uint16_t bytes) {
  if (!currentFile) return;
  
  // Отматываем назад на 500 байт (примерно страница)
  uint32_t pos = currentFile.position();
  if (pos > bytes) {
    currentFile.seek(pos - bytes);
  } else {
    currentFile.seek(0);
  }
  readFilePage();
}

// ================ ОТКРЫТИЕ ФАЙЛА ================
void openFile() {
  if (selectedFileName.length() == 0) {
    sdEnd();
    return;
  }

  currentFile = SD.open(selectedFileName.c_str(), FILE_READ);

  if (!currentFile || currentFile.isDirectory()) {
    if(currentFile.isDirectory()) currentFile.close();
    oled.clear();
    oled.home();
    oled.print("Oшибka");
    oled.setCursor(0, 2);
    oled.print(selectedFileName);
    oled.update();
    delay(1000);
    selectedFileName = "";
    sdEnd();
    sdStart();
    initSD();
    listFiles();
    sdEnd();
    return;
  }

  // Убираем ограничение на размер файла, так как теперь читаем постранично
  sdEnd();
  
  oled.inverse(false);
  readFilePage();
}

// ================ КАЛЬКУЛЯТОР (ЛОГИКА) ================
void calculatorLoop() {
  static uint8_t digitMode = 0;

  if (buttUP.clicked()) {
    calc.addDigit(digitMode);
    checkActivity();
  }

  if (buttDWN.clicked()) {
    if (calc.opSelectionMode) {
      calc.selectedOpIndex = (calc.selectedOpIndex + 1) % 4;
      calc.dirty = true;
    } else {
      digitMode = (digitMode + 1) % 10;
      calc.dirty = true;
    }
    checkActivity();
  }

  if (buttSET.clicked()) {
    if (calc.state == Calculator::INPUT_FIRST && !calc.opSelectionMode) {
      calc.opSelectionMode = true;
      calc.dirty = true;
    } else if (calc.opSelectionMode) {
      char opChar;
      switch (calc.selectedOpIndex) {
        case 0: opChar = '+'; break;
        case 1: opChar = '-'; break;
        case 2: opChar = '*'; break;
        case 3: opChar = '/'; break;
        default: opChar = '+';
      }
      calc.setOp(opChar);
    } else if (calc.state == Calculator::INPUT_SECOND) {
      calc.calculate();
    } else if (calc.state == Calculator::RESULT) {
      calc.clear();
    }
    checkActivity();
  }

  if (buttSET.holded()) {
    calc.clear();
    currentMode = MODE_MENU;
    drawMainMenu();
    return;
  }

  if (calc.dirty) {
    oled.clear();
    oled.home();

    oled.scale1X();
    oled.print(" Kaлbkyлятop");
    oled.setCursor(0, 1);
    oled.println("==============");

    oled.setCursor(0, 2);
    if (calc.opSelectionMode) {
      oled.print("Select op:");
    } else {
      switch (calc.state) {
        case Calculator::INPUT_FIRST: oled.print("A:"); break;
        case Calculator::INPUT_SECOND: oled.print("B:"); break;
        case Calculator::RESULT: oled.print("OTBET:"); break;
      }
    }

    oled.setCursor(0, 3);
    oled.scale2X();
    if (calc.opSelectionMode) {
      const char* opSymbols[] = {"  +  ", "  -  ", "  *  ", "  /  "};
      oled.print(opSymbols[calc.selectedOpIndex]);
    } else {
      char buf[16];
      calc.getDisplayString(buf, sizeof(buf));
      oled.print(buf);
    }
    oled.scale1X();

    if (calc.op && !calc.opSelectionMode) {
      oled.setCursor(0, 5);
      oled.print("Op: ");
      oled.print(calc.op);
    }

    oled.setCursor(0, 6);
    if (calc.opSelectionMode) {
      oled.print("DOWN:change");
      oled.setCursor(0, 7);
      oled.print("SET:confirm");
    } else {
      oled.print("UP:+");
      oled.print(digitMode);
      oled.print(" DOWN:next");
      oled.setCursor(0, 7);
      if (calc.state == Calculator::INPUT_FIRST) {
        oled.print("SET:choose op");
      } else if (calc.state == Calculator::INPUT_SECOND) {
        oled.print("SET:calc");
      } else {
        oled.print("SET:clear");
      }
    }

    oled.update();
    calc.dirty = false;
  }
}