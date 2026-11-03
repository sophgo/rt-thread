# How to run:
# 'pytest spacc_test_script.py', or 'pytest spacc_test_script.py::test_spacc_xxx', where 'xxx' is the algorithm name
import base64
import binascii
import random
import re
import secrets
import string
import time

import pytest
import serial

from cryptography.hazmat.primitives import padding, hashes
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.decrepit.ciphers.algorithms import TripleDES
from cryptography.hazmat.backends import default_backend


PORT = "COM3"  # Change to your serial port
BAUD_RATE = 115200
TEST_CASE_NUM = 10
MIN_PLAINTEXT_LEN, MAX_PLAINTEXT_LEN = 1, 55


class SPACCTestClient:
    def __init__(self, port, baudrate=115200, timeout=5):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.serial = None

    def connect(self):
        """Connect to the serial port"""
        self.serial = serial.Serial(
            port=self.port,
            baudrate=self.baudrate,
            timeout=self.timeout,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
        )

    def disconnect(self):
        """Close the serial connection"""
        if self.serial and self.serial.is_open:
            self.serial.close()

    def send_command(self, command, delay=0.1):
        """Send command to the board"""
        if not self.serial:
            raise Exception("Not connected to serial port")

        # Clear input buffer
        self.serial.reset_input_buffer()

        # Send command
        cmd = command + "\r\n"
        self.serial.write(cmd.encode("ascii"))
        self.serial.flush()

        if delay > 0:
            time.sleep(delay)

        # Read response
        response = ""
        while True:
            line = self.serial.readline().decode("ascii", errors="ignore")
            if not line:
                break
            response += line
            if "msh />" in line:  # RT-Thread shell prompt
                break

        return response

    @staticmethod
    def parse_symm_cipher_res(response):
        # Extract plaintext
        plaintext_hex_pattern = r"Plaintext \(HEX\):\s*((?:[0-9a-fA-F]{2})+)"
        plaintext_hex_match = re.search(plaintext_hex_pattern, response)
        plaintext = None
        if plaintext_hex_match:
            plaintext_hex = plaintext_hex_match.group(1)
            plaintext = binascii.unhexlify(plaintext_hex).decode("ascii")
        # Extract key
        key_hex_pattern = r"Key \(HEX\):\s*((?:[0-9a-fA-F]{2})+)"
        key_hex_match = re.search(key_hex_pattern, response)
        key = None
        if key_hex_match:
            key_hex = key_hex_match.group(1)
            key = binascii.unhexlify(key_hex).decode("ascii")
        # Extract iv
        iv_hex_pattern = r"IV \(HEX\):\s*((?:[0-9a-fA-F]{2})+)"
        iv_hex_match = re.search(iv_hex_pattern, response)
        iv = None
        if iv_hex_match:
            iv_hex = iv_hex_match.group(1)
            iv = binascii.unhexlify(iv_hex).decode("ascii")
        # Match data after "ENCRYPT_HEX:"
        encrypt_hex_pattern = r"Encrypted Data \(HEX\):\s*((?:[0-9a-fA-F]{2})+)"
        encrypt_hex_match = re.search(encrypt_hex_pattern, response)
        encrypt_hex = None
        if encrypt_hex_match:
            encrypt_hex = encrypt_hex_match.group(1)
        # Extract PASS/FAIL result
        res_pattern = r"Encrypt & Decrypt Result:\s*\[(PASS|FAIL)\]"
        res_match = re.search(res_pattern, response)
        res = res_match.group(1) if res_match else "FAIL"

        return (key, plaintext, iv), encrypt_hex, res

    @staticmethod
    def parse_hash_res(response):
        # Extract plaintext
        msg_hex_pattern = r"Msg \(HEX\):\s*((?:[0-9a-fA-F]{2})+)"
        msg_hex_match = re.search(msg_hex_pattern, response)
        msg = None
        if msg_hex_match:
            msg_hex = msg_hex_match.group(1)
            msg = binascii.unhexlify(msg_hex).decode("ascii")
        # Match data after "ENCRYPT_HEX:"
        hash_hex_pattern = r"Hash Data \(HEX\):\s*((?:[0-9a-fA-F]{2})+)"
        hash_hex_match = re.search(hash_hex_pattern, response)
        hash_hex = None
        if hash_hex_match:
            hash_hex = hash_hex_match.group(1)

        return msg, hash_hex

    @staticmethod
    def parse_base64_res(response):
        # Extract plaintext
        data_hex_pattern = r"Data \(HEX\):\s*((?:[0-9a-fA-F]{2})+)"
        data_hex_match = re.search(data_hex_pattern, response)
        data = None
        if data_hex_match:
            data_hex = data_hex_match.group(1)
            data = binascii.unhexlify(data_hex).decode("ascii")
        # Match data after "ENCRYPT_HEX:"
        hash_hex_pattern = r"Encode Data \(HEX\):\s*((?:[0-9a-fA-F]{2})+)"
        hash_hex_match = re.search(hash_hex_pattern, response)
        hash_hex = None
        if hash_hex_match:
            hash_hex = hash_hex_match.group(1)

        res_pattern = r"Encode & Decode Result:\s*\[(PASS|FAIL)\]"
        res_match = re.search(res_pattern, response)
        res = res_match.group(1) if res_match else "FAIL"

        return data, hash_hex, res


class SymmCipherReference:
    """Reference implementation using cryptography library"""

    @staticmethod
    def pad_data(data, block_size):
        """Pad data to be multiple of block size"""
        padder = padding.PKCS7(block_size).padder()
        padded_data = padder.update(data) + padder.finalize()
        return padded_data

    @staticmethod
    def encrypt(algo, plaintext, key, mode: str = "ECB", iv=None):
        algo = algo.upper()
        if algo not in ["TDES", "AES", "SM4"]:
            raise ValueError(f"Invalid algorithm: {algo}, choose from DES/TDES/AES/SM4")
        mode = mode.upper()
        if mode not in ["ECB", "CBC", "CTR"]:
            raise ValueError(f"Invalid mode: {mode}, choose from ECB/CBC/CTR")

        block_size = {
            "TDES": 64,
            "AES": 128,
            "SM4": 128,
        }[algo]
        # preprocess inputs
        padded_plaintext = SymmCipherReference.pad_data(
            plaintext.encode("ascii"), block_size
        )
        key = key.encode("ascii")
        if mode in ["CBC", "CTR"]:
            if iv is None:
                raise ValueError("IV is required for CBC and CTR modes")
            iv = iv.encode("ascii")

        cipher_algo = {
            "TDES": TripleDES,
            "AES": algorithms.AES,
            "SM4": algorithms.SM4,
        }[algo]
        cipher_mode = (
            modes.ECB()
            if mode == "ECB"
            else modes.CBC(iv) if mode == "CBC" else modes.CTR(iv)
        )
        # Create cipher object
        cipher = Cipher(
            cipher_algo(key),
            cipher_mode,
            backend=default_backend(),
        )
        encryptor = cipher.encryptor()
        return encryptor.update(padded_plaintext) + encryptor.finalize()


class HashReference:
    @staticmethod
    def hash(algo, msg):
        algo = algo.upper()
        if algo not in ["SHA1", "SHA256", "SM3"]:
            raise ValueError(f"Invalid algorithm: {algo}, choose from SHA1/SHA256/SM3")
        # Convert string data to bytes
        msg_bytes = msg.encode("ascii")

        # Select hash algorithm
        hash_algo = {
            "SHA1": hashes.SHA1(),
            "SHA256": hashes.SHA256(),
            "SM3": hashes.SM3(),
        }[algo]

        hasher = hashes.Hash(hash_algo, backend=default_backend())
        # Update and finalize hash
        hasher.update(msg_bytes)
        digest = hasher.finalize()
        return digest


# --- Helper functions ---
def gen_secure_printable_ascii(length):
    chars = string.ascii_letters + string.digits + " "
    return "".join(secrets.choice(chars) for _ in range(length))


# --- Test Fixtures ---
@pytest.fixture(scope="session")
def spacc_client():
    """Fixture to create a SPACC test client"""
    # You need to change this to your actual serial port
    client = SPACCTestClient(port=PORT, baudrate=BAUD_RATE)
    try:
        client.connect()
    except Exception as e:
        pytest.exit("\033[91mFailed to connect to SPACC test device\033[0m", returncode=1)
    yield client
    try:
        client.disconnect()
    except Exception as e:
        print(f"Warning: Failed to disconnect from SPACC test device: {e}")


@pytest.fixture
def test_tdes_case():
    mode = random.choice(["ECB", "CBC"])

    plaintext = gen_secure_printable_ascii(random.randint(MIN_PLAINTEXT_LEN, MAX_PLAINTEXT_LEN))
    key = gen_secure_printable_ascii(24)
    iv = gen_secure_printable_ascii(8) if mode in ["CBC"] else None
    return (mode, plaintext, key, iv)


@pytest.fixture
def test_aes_case():
    mode = random.choice(["ECB", "CBC", "CTR"])
    key_size = random.choice([16, 24, 32])  # Corresponds to AES-128, AES-192, AES-256

    plaintext = gen_secure_printable_ascii(random.randint(MIN_PLAINTEXT_LEN, MAX_PLAINTEXT_LEN))
    key = gen_secure_printable_ascii(key_size)
    iv = gen_secure_printable_ascii(16) if mode in ["CBC", "CTR"] else None
    return (mode, plaintext, key, iv)


@pytest.fixture
def test_sm4_case():
    mode = random.choice(["ECB", "CBC", "CTR"])

    plaintext = gen_secure_printable_ascii(random.randint(MIN_PLAINTEXT_LEN, MAX_PLAINTEXT_LEN))
    key = gen_secure_printable_ascii(16)
    iv = gen_secure_printable_ascii(16) if mode in ["CBC", "CTR"] else None
    return (mode, plaintext, key, iv)


@pytest.fixture
def test_hash_case():
    return gen_secure_printable_ascii(random.randint(MIN_PLAINTEXT_LEN, MAX_PLAINTEXT_LEN))


@pytest.fixture
def test_base64_case():
    return gen_secure_printable_ascii(random.randint(MIN_PLAINTEXT_LEN, MAX_PLAINTEXT_LEN))


def _run_test_symm_cipher(spacc_client, algo, mode, plaintext, key, iv):
    mode_map = {"ECB": 0, "CBC": 1, "CTR": 2}
    # Get reference encryption result
    reference = SymmCipherReference.encrypt(algo, plaintext, key, mode=mode, iv=iv)
    expected_hex = binascii.hexlify(reference).decode("ascii")

    spacc_client.send_command(" ")
    command = (
        f'spacc_test_symm_cipher -{algo.lower()} -m {mode_map[mode.upper()]} -k "{key}" -in "{plaintext}"'
        + (f' -iv "{iv}"' if iv is not None else "")
    )
    raw_response = spacc_client.send_command(command)
    try:
        (actual_key, actual_plaintext, actual_iv), actual_hex, res = (
            spacc_client.parse_symm_cipher_res(raw_response)
        )
        print(f"\n=== {algo} Encryption Test ===")
        print(f"Command:\t{command}")
        print(f"Actual Key:\t{actual_key}")
        print(f"Actual Plaintext:\t{actual_plaintext}")
        print(f"Actual IV:\t{actual_iv}")
        print(f"Expected Encrypt Result:\t{expected_hex}")
        print(f"Actual Encrypt Result:\t{actual_hex}")
        print("=== Raw Response ===\n", raw_response)
        assert res == "PASS", "Encryption & Decryption failed"
        assert expected_hex == actual_hex, "Encryption output mismatch"

    except ValueError as e:
        print(f"\nError extracting encrypted data: {e}")
        print("=== Raw Response ===\n", raw_response)
        assert False, "Test failed due to unkown/extraction error"


def _run_test_hash(spacc_client, algo, msg):
    # Get reference hash result
    reference = HashReference.hash(algo, msg)
    expected_hex = binascii.hexlify(reference).decode("ascii")

    spacc_client.send_command(" ")
    command = f'spacc_test_hash -{algo.lower()} -in "{msg}"'

    raw_response = spacc_client.send_command(command)
    try:
        actual_msg, actual_hex = spacc_client.parse_hash_res(raw_response)
        print(f"\n=== {algo} Hash Test ===")
        print(f"Command:\t{command}")
        print(f"Actual Message:\t{actual_msg}")
        print(f"Expected Hash Result:\t{expected_hex}")
        print(f"Actual Hash Result:\t{actual_hex}")
        print("=== Raw Response ===\n", raw_response)
        assert expected_hex == actual_hex, "Hash output mismatch"

    except ValueError as e:
        print(f"\nError extracting hash data: {e}")
        print("=== Raw Response ===\n", raw_response)
        assert False, "Test failed due to unkown/extraction error"


@pytest.mark.parametrize("test_aes_case", range(TEST_CASE_NUM), indirect=True)
def test_spacc_aes(spacc_client, test_aes_case):
    """Test AES encryption with various modes, keys, and plaintexts"""
    mode, plaintext, key, iv = test_aes_case
    _run_test_symm_cipher(spacc_client, "AES", mode, plaintext, key, iv)


@pytest.mark.parametrize("test_sm4_case", range(TEST_CASE_NUM), indirect=True)
def test_spacc_sm4(spacc_client, test_sm4_case):
    """Test SM4 encryption with various modes, keys, and plaintexts"""
    mode, plaintext, key, iv = test_sm4_case
    _run_test_symm_cipher(spacc_client, "SM4", mode, plaintext, key, iv)


@pytest.mark.parametrize("test_tdes_case", range(TEST_CASE_NUM), indirect=True)
def test_spacc_tdes(spacc_client, test_tdes_case):
    """Test TDES encryption with various modes, keys, and plaintexts"""
    mode, plaintext, key, iv = test_tdes_case
    _run_test_symm_cipher(spacc_client, "TDES", mode, plaintext, key, iv)


@pytest.mark.parametrize("test_hash_case", range(TEST_CASE_NUM), indirect=True)
def test_spacc_sha1(spacc_client, test_hash_case):
    msg = test_hash_case
    _run_test_hash(spacc_client, "SHA1", msg)


@pytest.mark.parametrize("test_hash_case", range(TEST_CASE_NUM), indirect=True)
def test_spacc_sha256(spacc_client, test_hash_case):
    msg = test_hash_case
    _run_test_hash(spacc_client, "SHA256", msg)


@pytest.mark.skip(reason="SM3 Hash Algorithmn is Not implemented")
@pytest.mark.parametrize("test_hash_case", range(TEST_CASE_NUM), indirect=True)
def test_spacc_sm3(spacc_client, test_hash_case):
    msg = test_hash_case
    _run_test_hash(spacc_client, "sm3", msg)


@pytest.mark.parametrize("test_base64_case", range(TEST_CASE_NUM), indirect=True)
def test_spacc_base64(spacc_client, test_base64_case):
    """Test Base64 encoding/decoding"""
    data = test_base64_case

    reference = base64.b64encode(data.encode("ascii"))
    expected_hex = binascii.hexlify(reference).decode("ascii")

    print("\nData HEX:", binascii.hexlify(data.encode("ascii")).decode("ascii"))

    spacc_client.send_command(" ")
    command = f'spacc_test_base64_dynm -in "{data}"'
    raw_response = spacc_client.send_command(command)
    try:
        actual_data, actual_hex, res = spacc_client.parse_base64_res(raw_response)

        print(f"\n=== Base64 Encode & Decode Test ===")
        print(f"Command:\t{command}")
        print(f"Actual Data:\t{actual_data}")
        print(f"Expected Encode Result:\t{expected_hex}")
        print(f"Actual Encode Result:\t{actual_hex}")
        print("=== Raw Response ===\n", raw_response)
        assert res == "PASS", "Encode & Decode failed"
        assert expected_hex == actual_hex, "Encode output mismatch"

    except ValueError as e:
        print(f"\nError extracting encoded data: {e}")
        print("=== Raw Response ===\n", raw_response)
        assert False, "Test failed due to unkown/extraction error"


if __name__ == "__main__":
    pytest.main()
