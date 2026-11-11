"""HX711 (v10): lógica C real del conversor, constructor de la trama y verificador; no necesitan QEMU."""
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

import qemu_prueba


class PruebaHx711(unittest.TestCase):
    def test_logica_c(self):
        cc = (shutil.which("cc") or shutil.which("gcc") or shutil.which("clang") or
              shutil.which("cl"))
        if cc is None:
            self.skipTest("compilador C no disponible")
        raiz = Path(__file__).resolve().parents[1]
        fuente = raiz / "pruebas" / "hx711_logica.c"
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "hx711.exe"
            if Path(cc).stem.lower() == "cl":
                comando = [cc, "/nologo", "/std:c11", "/W4", "/WX", "/wd4100",
                           "/wd4244", "/wd4267", "/utf-8", f"/I{raiz / 'capas/include'}",
                           str(fuente), f"/Fe:{exe}", f"/Fo:{Path(tmp) / 'hx711.obj'}"]
            else:
                comando = [cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic",
                           "-I", str(raiz / "capas/include"), str(fuente), "-o", str(exe)]
            subprocess.run(comando, check=True)
            subprocess.run([str(exe)], check=True)

    def test_trama(self):
        t = qemu_prueba.trama_hx711(17, 16, -123456, 54321, -61728)
        self.assertEqual(t[:4], struct.pack("<BBH", 23, 10, 15))
        self.assertEqual(t[12:], struct.pack("<BBiiiB", 17, 16, -123456, 54321, -61728, 1))
        self.assertEqual(qemu_prueba.trama_hx711(17, 16, 0, 0, 0, presente=False, rapido=True)[-1], 2)
        with self.assertRaises(ValueError):
            qemu_prueba.trama_hx711(16, 16, 0, 0, 0)

    def test_verificador(self):
        q = qemu_prueba
        bueno = (f"ESPERA=401\r\nA128={q.HX711_A128}\r\nA64={q.HX711_A64}\r\nB32={q.HX711_B32}\r\n"
                 f"TARA={q.HX711_A128} EN=505\r\nHX711-CAMBIO\r\nNETO={q.HX711_A128_NUEVA - q.HX711_A128}\r\n"
                 f"DESPIERTA=400 A128={q.HX711_A128_NUEVA}\r\nTALLER-OK hx711\r\n")
        self.assertEqual(q.comprobar_hx711(bueno), [])
        self.assertTrue(q.comprobar_hx711(bueno.replace("ESPERA=401", "ESPERA=5")))
        self.assertTrue(q.comprobar_hx711(bueno.replace(f"B32={q.HX711_B32}", "B32=-1")))
        self.assertTrue(q.comprobar_hx711(bueno.replace("DESPIERTA=400", "DESPIERTA=0")))


if __name__ == "__main__":
    unittest.main()
