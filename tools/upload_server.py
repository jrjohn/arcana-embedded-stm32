#!/usr/bin/env python3
"""
Arcana Upload Server — receives .ats file uploads via chunked/streaming POST.

Endpoints:
  POST /upload/<device_id>/<filename>
    - Accepts gzip-compressed stream (Content-Encoding: gzip)
    - Or raw binary stream
    - Supports Content-Range for resume
    - Stores to ./uploads/<device_id>/<filename>

  GET /uploads/<device_id>
    - List uploaded files for a device

  GET /health
    - Server health check

Usage:
  python3 tools/upload_server.py
  python3 tools/upload_server.py --port 8080 --upload-dir ./uploads
"""

import os
import gzip
import hashlib
import json
import time
import argparse
from pathlib import Path
from flask import Flask, request, jsonify, abort

app = Flask(__name__)

UPLOAD_DIR = "./uploads"


@app.route("/health", methods=["GET"])
def health():
    return jsonify({"status": "ok", "time": int(time.time())})


@app.route("/upload/<device_id>/<filename>", methods=["POST"])
def upload_file(device_id, filename):
    """
    Receive .ats file upload (gzip stream or raw).

    Headers:
      Content-Encoding: gzip  (optional, auto-decompress)
      Content-Range: bytes <start>-<end>/<total>  (optional, for resume)
      X-Checksum-SHA256: <hex>  (optional, verify after write)
    """
    # Validate filename
    if not filename.endswith(".ats"):
        abort(400, "Only .ats files accepted")
    if "/" in filename or ".." in filename:
        abort(400, "Invalid filename")

    # Create device directory
    device_dir = Path(UPLOAD_DIR) / device_id
    device_dir.mkdir(parents=True, exist_ok=True)
    filepath = device_dir / filename

    # Parse Content-Range for resume
    content_range = request.headers.get("Content-Range")
    write_offset = 0
    total_size = None
    is_partial = False

    if content_range:
        # Format: bytes 0-4095/131072
        try:
            range_spec = content_range.replace("bytes ", "")
            range_part, total_part = range_spec.split("/")
            start, end = range_part.split("-")
            write_offset = int(start)
            total_size = int(total_part) if total_part != "*" else None
            is_partial = True
        except (ValueError, IndexError):
            abort(400, f"Invalid Content-Range: {content_range}")

    # Check if gzip
    is_gzip = request.headers.get("Content-Encoding", "").lower() == "gzip"

    # Read stream
    raw_data = request.get_data()
    if is_gzip:
        try:
            data = gzip.decompress(raw_data)
        except Exception as e:
            abort(400, f"Gzip decompress failed: {e}")
    else:
        data = raw_data

    # Write to file
    mode = "r+b" if (is_partial and filepath.exists()) else "wb"
    if is_partial and filepath.exists():
        with open(filepath, mode) as f:
            f.seek(write_offset)
            f.write(data)
    else:
        with open(filepath, "wb") as f:
            f.write(data)

    # Compute checksum of written portion
    sha256 = hashlib.sha256(data).hexdigest()

    # Verify checksum if provided
    expected_checksum = request.headers.get("X-Checksum-SHA256")
    checksum_ok = True
    if expected_checksum:
        checksum_ok = sha256 == expected_checksum.lower()

    # File stats
    file_size = filepath.stat().st_size
    complete = (total_size is not None and file_size >= total_size) or not is_partial

    result = {
        "status": "complete" if complete else "partial",
        "device_id": device_id,
        "filename": filename,
        "written_bytes": len(data),
        "offset": write_offset,
        "file_size": file_size,
        "sha256": sha256,
        "checksum_ok": checksum_ok,
    }

    if total_size:
        result["total_size"] = total_size
        result["progress"] = f"{file_size}/{total_size} ({100*file_size//total_size}%)"

    print(f"[UPLOAD] {device_id}/{filename}: {len(data)}B @ offset {write_offset} "
          f"→ {file_size}B {'COMPLETE' if complete else 'PARTIAL'}")

    return jsonify(result), 200 if complete else 206


@app.route("/uploads/<device_id>", methods=["GET"])
def list_files(device_id):
    """List uploaded files for a device."""
    device_dir = Path(UPLOAD_DIR) / device_id
    if not device_dir.exists():
        return jsonify({"device_id": device_id, "files": []})

    files = []
    for f in sorted(device_dir.iterdir()):
        if f.is_file():
            stat = f.stat()
            files.append({
                "name": f.name,
                "size": stat.st_size,
                "modified": int(stat.st_mtime),
                "sha256": hashlib.sha256(f.read_bytes()).hexdigest(),
            })

    return jsonify({
        "device_id": device_id,
        "files": files,
        "count": len(files),
    })


@app.route("/upload/<device_id>/<filename>/status", methods=["GET"])
def upload_status(device_id, filename):
    """Check upload status (for resume)."""
    filepath = Path(UPLOAD_DIR) / device_id / filename
    if not filepath.exists():
        return jsonify({"exists": False, "size": 0})

    return jsonify({
        "exists": True,
        "size": filepath.stat().st_size,
        "sha256": hashlib.sha256(filepath.read_bytes()).hexdigest(),
    })


def generate_self_signed_cert(cert_dir="./certs"):
    """Generate self-signed cert if not exists. Returns (cert_path, key_path)."""
    os.makedirs(cert_dir, exist_ok=True)
    cert_path = os.path.join(cert_dir, "server.crt")
    key_path = os.path.join(cert_dir, "server.key")

    if os.path.exists(cert_path) and os.path.exists(key_path):
        return cert_path, key_path

    try:
        from cryptography import x509
        from cryptography.x509.oid import NameOID
        from cryptography.hazmat.primitives import hashes, serialization
        from cryptography.hazmat.primitives.asymmetric import rsa
        import datetime

        key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
        subject = issuer = x509.Name([
            x509.NameAttribute(NameOID.COMMON_NAME, "arcana-upload"),
            x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Arcana"),
        ])
        cert = (x509.CertificateBuilder()
                .subject_name(subject)
                .issuer_name(issuer)
                .public_key(key.public_key())
                .serial_number(x509.random_serial_number())
                .not_valid_before(datetime.datetime.utcnow())
                .not_valid_after(datetime.datetime.utcnow() + datetime.timedelta(days=3650))
                .add_extension(x509.SubjectAlternativeName([
                    x509.DNSName("localhost"),
                    x509.IPAddress(ipaddress.IPv4Address("0.0.0.0")),
                ]), critical=False)
                .sign(key, hashes.SHA256()))

        with open(key_path, "wb") as f:
            f.write(key.private_bytes(
                serialization.Encoding.PEM,
                serialization.PrivateFormat.TraditionalOpenSSL,
                serialization.NoEncryption()))
        with open(cert_path, "wb") as f:
            f.write(cert.public_bytes(serialization.Encoding.PEM))

        print(f"  Generated self-signed cert: {cert_path}")
        return cert_path, key_path

    except ImportError:
        # Fallback: use openssl CLI
        import subprocess
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048",
            "-keyout", key_path, "-out", cert_path,
            "-days", "3650", "-nodes",
            "-subj", "/CN=arcana-upload/O=Arcana"
        ], check=True, capture_output=True)
        print(f"  Generated self-signed cert (openssl): {cert_path}")
        return cert_path, key_path


if __name__ == "__main__":
    import ipaddress

    parser = argparse.ArgumentParser(description="Arcana Upload Server (HTTPS)")
    parser.add_argument("--port", type=int, default=443)
    parser.add_argument("--host", default="0.0.0.0")
    parser.add_argument("--upload-dir", default="./uploads")
    parser.add_argument("--cert-dir", default="./certs")
    parser.add_argument("--no-ssl", action="store_true", help="Run without HTTPS (plain HTTP)")
    args = parser.parse_args()

    UPLOAD_DIR = args.upload_dir
    os.makedirs(UPLOAD_DIR, exist_ok=True)

    ssl_ctx = None
    proto = "http"
    if not args.no_ssl:
        cert_path, key_path = generate_self_signed_cert(args.cert_dir)
        import ssl
        ssl_ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        ssl_ctx.load_cert_chain(cert_path, key_path)
        proto = "https"

    print(f"Arcana Upload Server")
    print(f"  Upload dir: {os.path.abspath(UPLOAD_DIR)}")
    print(f"  Listening:  {proto}://{args.host}:{args.port}")
    print(f"  SSL:        {'self-signed' if ssl_ctx else 'disabled'}")
    print(f"  Endpoints:")
    print(f"    POST /upload/<device_id>/<filename>  (gzip or raw)")
    print(f"    GET  /uploads/<device_id>            (list files)")
    print(f"    GET  /upload/<device_id>/<filename>/status  (resume check)")
    print()

    app.run(host=args.host, port=args.port, debug=True, ssl_context=ssl_ctx)
