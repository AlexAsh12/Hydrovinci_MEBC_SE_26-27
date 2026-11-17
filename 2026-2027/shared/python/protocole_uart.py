"""
Protocole UART ESP32 <-> Pi, cote Python (Pi Zero).

Specification : ../PROTOCOLE_UART.md

Trame = une ligne : <JSON>*<CRC16 en 4 hex majuscules>\\n

Utilisation typique sur la Pi :

    import serial
    from protocole_uart import Decodeur, Encodeur

    port = serial.Serial("/dev/serial0", 115200, timeout=0.1)
    dec = Decodeur()
    enc = Encodeur()

    port.write(enc.encoder({"t": "ping"}))
    while True:
        for msg in dec.ajouter(port.read(256)):
            print(msg["t"], msg)

Ce module n'a aucune dependance (pas meme pyserial) pour pouvoir etre
teste sur n'importe quel PC.
"""

import json

VERSION_PROTOCOLE = 1
TAILLE_MAX_LIGNE = 1024
TIMEOUT_LIAISON_S = 3.0
SEQ_MAX = 65535


def crc16_ccitt(data: bytes) -> int:
    """CRC-16/CCITT-FALSE : poly 0x1021, init 0xFFFF, sans reflexion, xorout 0."""
    crc = 0xFFFF
    for octet in data:
        crc ^= octet << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def encoder_trame(message: dict) -> bytes:
    """Transforme un message (dict) en trame prete a envoyer sur l'UART."""
    corps = json.dumps(message, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    return corps + b"*" + f"{crc16_ccitt(corps):04X}".encode("ascii") + b"\n"


def decoder_ligne(ligne: bytes):
    """
    Decode une ligne (sans le \\n). Renvoie le message (dict) ou None si la
    trame est invalide (pas de '*', CRC faux, JSON invalide, pas de champ 't').
    """
    ligne = ligne.rstrip(b"\r")
    # Une trame commence toujours par '{' : on ignore les parasites qui precedent
    debut = ligne.find(b"{")
    if debut < 0:
        return None
    corps, sep, crc_txt = ligne[debut:].rpartition(b"*")
    if not sep or len(crc_txt) != 4:
        return None
    try:
        crc_recu = int(crc_txt, 16)
    except ValueError:
        return None
    if crc_recu != crc16_ccitt(corps):
        return None
    try:
        message = json.loads(corps.decode("ascii"))
    except (UnicodeDecodeError, ValueError):
        return None
    if not isinstance(message, dict) or not isinstance(message.get("t"), str):
        return None
    return message


class Encodeur:
    """Ajoute automatiquement le numero de sequence 'n' a chaque message envoye."""

    def __init__(self):
        self._seq = 0

    def encoder(self, message: dict) -> bytes:
        message = dict(message)
        message["n"] = self._seq
        self._seq = (self._seq + 1) % (SEQ_MAX + 1)
        return encoder_trame(message)


class Decodeur:
    """
    Recoit les octets bruts de l'UART (morceaux de n'importe quelle taille)
    et renvoie les messages complets et valides.
    """

    def __init__(self):
        self._tampon = bytearray()
        self._trop_long = False
        self.trames_ok = 0
        self.trames_erreur = 0

    def ajouter(self, donnees: bytes) -> list:
        messages = []
        for octet in donnees:
            if octet == 0x0A:  # '\n' : fin de trame
                if self._trop_long:
                    self.trames_erreur += 1
                elif self._tampon:
                    message = decoder_ligne(bytes(self._tampon))
                    if message is None:
                        self.trames_erreur += 1
                    else:
                        self.trames_ok += 1
                        messages.append(message)
                self._tampon.clear()
                self._trop_long = False
            elif not self._trop_long:
                self._tampon.append(octet)
                if len(self._tampon) > TAILLE_MAX_LIGNE:
                    # Ligne trop longue : on jette tout jusqu'au prochain '\n'
                    self._tampon.clear()
                    self._trop_long = True
        return messages
