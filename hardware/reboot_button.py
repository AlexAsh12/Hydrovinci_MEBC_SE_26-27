import RPi.GPIO as GPIO
import os
import time

BUTTON_PIN = 6          # À adapter si tu utilises un autre GPIO
LONG_PRESS_TIME = 2      # Durée d'appui long en secondes

GPIO.setmode(GPIO.BCM)
GPIO.setup(BUTTON_PIN, GPIO.IN, pull_up_down=GPIO.PUD_UP)

print("Surveillance du bouton en cours (appui long = reboot)...")

try:
    while True:
        button_pressed_time = None
        # Attente d'un appui
        if GPIO.input(BUTTON_PIN) == False:
            button_pressed_time = time.time()
            # Tant que le bouton reste pressé
            while GPIO.input(BUTTON_PIN) == False:
                time.sleep(0.1)
                # Si l'appui long dépasse la durée choisie
                if time.time() - button_pressed_time > LONG_PRESS_TIME:
                    print("Appui long détecté, redémarrage...")
                    os.system("sudo reboot")
                    break  # Pour éviter plusieurs reboot dans la même pression
        time.sleep(0.1)

except KeyboardInterrupt:
    print("Arrêt manuel")
finally:
    GPIO.cleanup()
