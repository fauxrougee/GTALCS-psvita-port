"""French fix pour reLCS.

Le FRENCH.GXT converti depuis la version PS2 n'a pas les textes propres a la
version PC (menu principal, options, commandes...). Ce script ajoute a la table
MAIN chaque cle presente dans AMERICAN.GXT mais absente du francais, avec la
traduction de fr_missing.txt si elle existe, sinon le texte anglais.

Usage : python frenchfix.py [dossier TEXT]
L'original est sauvegarde en FRENCH.GXT.orig (et reutilise aux executions suivantes).
"""
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def read_gxt(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'TABL', path
    n = struct.unpack_from('<i', b, 4)[0] // 12
    tables = [(b[8 + i*12:16 + i*12], struct.unpack_from('<I', b, 16 + i*12)[0]) for i in range(n)]
    return b, tables


def read_table(b, p):
    """Lit TKEY+TDAT a la position p. Renvoie ({cle: [wchar]}, fin)."""
    assert b[p:p+4] == b'TKEY'
    ksize = struct.unpack_from('<i', b, p + 4)[0]
    q = p + 8 + ksize
    assert b[q:q+4] == b'TDAT'
    dsize = struct.unpack_from('<i', b, q + 4)[0]
    data = b[q + 8:q + 8 + dsize]
    out = {}
    for i in range(ksize // 12):
        vo = struct.unpack_from('<I', b, p + 8 + i*12)[0]
        key = b[p + 12 + i*12:p + 20 + i*12].rstrip(b'\0').decode('ascii')
        s = []
        while True:
            c = struct.unpack_from('<H', data, vo)[0]
            if c == 0:
                break
            s.append(c)
            vo += 2
        out[key] = s
    return out, q + 8 + dsize


def build_table(entries):
    keys = sorted(entries)  # le jeu fait une recherche dichotomique (strcmp)
    tkey, tdat = bytearray(), bytearray()
    for k in keys:
        tkey += struct.pack('<I', len(tdat)) + k.encode('ascii').ljust(8, b'\0')
        for c in entries[k] + [0]:
            tdat += struct.pack('<H', c)
    return b'TKEY' + struct.pack('<i', len(tkey)) + tkey + b'TDAT' + struct.pack('<i', len(tdat)) + tdat


def encode_fr(text):
    """Encodage GXT : ASCII tel quel, lettres accentuees Latin-1 decalees de 0x40."""
    out = []
    for ch in text:
        c = ord(ch)
        if c < 128:
            out.append(c)
        elif 0xC0 <= c <= 0xFF:
            out.append(c - 0x40)
        else:
            raise ValueError('caractere non gere : %r dans %r' % (ch, text))
    return out


def load_translations():
    """Renvoie (traductions des cles manquantes, remplacements forces des cles '!CLE')."""
    tr, forced = {}, {}
    with open(os.path.join(HERE, 'fr_missing.txt'), encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            key, text = line.split('\t', 1)
            if key.startswith('!'):
                forced[key[1:]] = encode_fr(text)
            else:
                tr[key] = encode_fr(text)
    return tr, forced


def main():
    textdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, '..', '..', 'assets', 'TEXT')
    fr_path = os.path.join(textdir, 'FRENCH.GXT')
    orig = fr_path + '.orig'
    if not os.path.exists(orig):
        shutil.copyfile(fr_path, orig)

    ba, ta = read_gxt(os.path.join(textdir, 'AMERICAN.GXT'))
    bf, tf = read_gxt(orig)
    assert tf[0][0].rstrip(b'\0') == b'MAIN' and ta[0][0].rstrip(b'\0') == b'MAIN'

    main_en, _ = read_table(ba, ta[0][1])
    main_fr, main_end = read_table(bf, tf[0][1])
    translations, forced = load_translations()

    added = translated = 0
    for k, v in main_en.items():
        if k not in main_fr:
            main_fr[k] = translations.get(k, v)
            added += 1
            translated += k in translations
    main_fr.update(forced)
    new_main = build_table(main_fr)
    new_main += b'\0' * (-(tf[0][1] + len(new_main)) % 4)  # tables alignees sur 4 octets

    # Les tables de mission sont recopiees telles quelles, decalees.
    first_mission = min(off for _, off in tf[1:])
    assert first_mission - main_end < 4 and not bf[main_end:first_mission].strip(b'\0'), \
        'donnees inattendues apres MAIN'
    header_len = tf[0][1]
    delta = header_len + len(new_main) - first_mission

    out = bytearray(bf[:header_len])
    for i, (name, off) in enumerate(tf):
        new_off = off if i == 0 else off + delta
        struct.pack_into('<I', out, 16 + i*12, new_off)
    out += new_main
    out += bf[first_mission:]

    with open(fr_path, 'wb') as f:
        f.write(out)

    # Verification : relecture complete du fichier genere.
    b2, t2 = read_gxt(fr_path)
    check, _ = read_table(b2, t2[0][1])
    assert check == main_fr
    for (name, off), (_, off2) in zip(tf[1:], t2[1:]):
        assert b2[off2:off2 + 8] == name and b2[off2 + 8:off2 + 12] == b'TKEY'
    print('FRENCH.GXT corrige : %d textes ajoutes (%d traduits en francais, %d en anglais), %d remplaces.'
          % (added, translated, added - translated, len(forced)))


if __name__ == '__main__':
    main()
