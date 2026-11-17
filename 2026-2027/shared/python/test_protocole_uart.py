"""
Tests du protocole UART cote Python.

Lancer depuis ce dossier :  python -m unittest -v test_protocole_uart
"""

import unittest

from protocole_uart import (
    TAILLE_MAX_LIGNE,
    Decodeur,
    Encodeur,
    crc16_ccitt,
    decoder_ligne,
    encoder_trame,
)

# Valeurs de reference, identiques a celles de la librairie C++ et de la spec.
TRAME_HB = b'{"t":"hb","n":42,"up":125000}*'


class TestCrc(unittest.TestCase):
    def test_valeur_de_reference(self):
        self.assertEqual(crc16_ccitt(b"123456789"), 0x29B1)

    def test_vide(self):
        self.assertEqual(crc16_ccitt(b""), 0xFFFF)


class TestEncodage(unittest.TestCase):
    def test_format(self):
        trame = encoder_trame({"t": "hb", "n": 42, "up": 125000})
        self.assertTrue(trame.startswith(TRAME_HB))
        self.assertTrue(trame.endswith(b"\n"))
        self.assertEqual(len(trame), len(TRAME_HB) + 4 + 1)

    def test_aller_retour(self):
        msg = {"t": "bms", "n": 1, "id": 1, "ok": True, "cells": [3.24, 3.22], "soc": None}
        self.assertEqual(decoder_ligne(encoder_trame(msg).rstrip(b"\n")), msg)

    def test_sequence_incrementee_et_rebouclee(self):
        enc = Encodeur()
        enc._seq = 65535
        dec = Decodeur()
        msgs = dec.ajouter(enc.encoder({"t": "ping"}) + enc.encoder({"t": "ping"}))
        self.assertEqual([m["n"] for m in msgs], [65535, 0])

    def test_accents_echappes_en_ascii(self):
        trame = encoder_trame({"t": "log", "n": 0, "msg": "été"})
        trame.decode("ascii")  # ne doit pas lever d'exception


class TestDecodage(unittest.TestCase):
    def test_crc_faux(self):
        trame = encoder_trame({"t": "hb", "n": 1}).rstrip(b"\n")
        corrompue = trame.replace(b'"n":1', b'"n":2')
        self.assertIsNone(decoder_ligne(corrompue))

    def test_sans_etoile(self):
        self.assertIsNone(decoder_ligne(b'{"t":"hb","n":1}'))

    def test_crc_non_hexa(self):
        self.assertIsNone(decoder_ligne(b'{"t":"hb"}*ZZZZ'))

    def test_sans_type(self):
        self.assertIsNone(decoder_ligne(encoder_trame({"n": 1}).rstrip(b"\n")))

    def test_json_non_objet(self):
        self.assertIsNone(decoder_ligne(encoder_trame([1, 2]).rstrip(b"\n")))

    def test_crlf_accepte(self):
        trame = encoder_trame({"t": "hb", "n": 3}).replace(b"\n", b"\r\n")
        self.assertEqual(Decodeur().ajouter(trame), [{"t": "hb", "n": 3}])

    def test_etoile_dans_le_texte(self):
        msg = {"t": "log", "n": 0, "msg": "a*b"}
        self.assertEqual(decoder_ligne(encoder_trame(msg).rstrip(b"\n")), msg)


class TestFlux(unittest.TestCase):
    def test_trame_coupee_en_morceaux(self):
        trame = encoder_trame({"t": "hb", "n": 5, "up": 1})
        dec = Decodeur()
        recus = []
        for i in range(len(trame)):
            recus += dec.ajouter(trame[i:i + 1])
        self.assertEqual(recus, [{"t": "hb", "n": 5, "up": 1}])

    def test_resynchronisation_apres_parasite(self):
        dec = Decodeur()
        flux = (b"\x00\xff parasite" + encoder_trame({"t": "hb", "n": 1})
                + b"bruit\n" + encoder_trame({"t": "hb", "n": 2}))
        msgs = dec.ajouter(flux)
        self.assertEqual([m["n"] for m in msgs], [1, 2])
        self.assertEqual(dec.trames_erreur, 1)  # seule la ligne "bruit" est perdue
        self.assertEqual(dec.trames_ok, 2)

    def test_ligne_trop_longue_jetee(self):
        dec = Decodeur()
        flux = b"x" * (TAILLE_MAX_LIGNE + 50) + b"\n" + encoder_trame({"t": "hb", "n": 9})
        msgs = dec.ajouter(flux)
        self.assertEqual(msgs, [{"t": "hb", "n": 9}])
        self.assertEqual(dec.trames_erreur, 1)

    def test_lignes_vides_ignorees(self):
        dec = Decodeur()
        self.assertEqual(dec.ajouter(b"\n\n\n"), [])
        self.assertEqual(dec.trames_erreur, 0)


if __name__ == "__main__":
    unittest.main()
