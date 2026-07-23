import sys
from cryptography.hazmat.primitives.ciphers.aead import AESGCM

def main():
    if len(sys.argv) != 4:
        print("usage: python3 reveal.py <fragment1_hex> <fragment2_hex> <fragment3_hex>")
        sys.exit(1)

    key_hex = sys.argv[1] + sys.argv[2] + sys.argv[3]
    key = bytes.fromhex(key_hex)
    if len(key) != 32:
        print("assembled key is not 32 bytes, check your fragments and order")
        sys.exit(1)

    with open("echoes.enc", "rb") as f:
        blob = f.read()

    nonce, ciphertext = blob[:12], blob[12:]
    aesgcm = AESGCM(key)
    try:
        plaintext = aesgcm.decrypt(nonce, ciphertext, None)
    except Exception:
        print("decryption failed, wrong key or corrupted file")
        sys.exit(1)

    text = plaintext.decode("utf-8")
    out_path = "echoes.txt"
    with open(out_path, "w", encoding="utf-8") as f:
        f.write(text)

    print(text)
    print(f"\nsaved to {out_path}")

if __name__ == "__main__":
    main()
