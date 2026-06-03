from gpiozero import LED
from time import sleep
import sys

led1 = LED(17)
UNIT = 0.06

def flash_letter(symbols):
    for i, symbol in enumerate(symbols):
        led1.on()
        sleep(UNIT if symbol == '.' else UNIT * 3)
        led1.off()
        if i < len(symbols) - 1:
            sleep(UNIT)      # intra-letter gap between symbols
def flash_message(message):
    words = message.upper().split(' ')
    for w, word in enumerate(words):
        for l, letter in enumerate(word):
            if letter in morse_code_dict:
                flash_letter(morse_code_dict[letter])
            if l < len(word) - 1:
                sleep(UNIT * 3)
        if w < len(words) - 1:
            sleep(UNIT * 7)

morse_code_dict = {
    'A': '.-', 'B': '-...', 'C': '-.-.', 'D': '-..', 'E': '.',
    'F': '..-.', 'G': '--.', 'H': '....', 'I': '..', 'J': '.---',
    'K': '-.-', 'L': '.-..', 'M': '--', 'N': '-.', 'O': '---',
    'P': '.--.', 'Q': '--.-', 'R': '.-.', 'S': '...', 'T': '-',
    'U': '..-', 'V': '...-', 'W': '.--', 'X': '-..-', 'Y': '-.--',
    'Z': '--..',
    '0': '-----', '1': '.----', '2': '..---', '3': '...--', '4': '....-',
    '5': '.....', '6': '-....', '7': '--...', '8': '---..', '9': '----.',
    ' ': ' '
}

if __name__ == "__main__":
    numTx =int( sys.argv[1])
    message = " ".join(sys.argv[2:])
    print(f"Message: {message}")
    for i in range(numTx):
        flash_message(message)
    print("\nDone!")
