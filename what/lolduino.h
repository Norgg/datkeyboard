#pragma once

typedef unsigned int uint;

#define INPUT_PULLUP 1
#define OUTPUT 1
#define KEY_BACKSPACE 10

void pinMode(int pin, int out);

char lookup(int first, int second);

class KeyboardCls {
    public:
        void begin() {}
        void releaseAll() {}
        void press(int key) {}
};

static void delay(int amt) {}

void loop();

int main() {
  while (true) {
    loop();
  }
}

static KeyboardCls Keyboard;
