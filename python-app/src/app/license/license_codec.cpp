#include "app/license/license_codec.h"

namespace license {

namespace {
// Crockford base32 alphabet: 0-9 then A-Z with I, L, O, U removed.
constexpr const char *kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

// Map a single input character to its 5-bit value, or -1 if not a symbol.
// Case-insensitive; folds the ambiguous letters (I/L -> 1, O -> 0) the way
// Crockford specifies so hand-transcribed keys still decode.
int symbolValue(QChar ch) {
  const char c = ch.toUpper().toLatin1();
  if (c >= '0' && c <= '9')
    return c - '0';
  switch (c) {
  case 'O':
    return 0;
  case 'I':
  case 'L':
    return 1;
  default:
    break;
  }
  for (int i = 10; i < 32; ++i) {
    if (kAlphabet[i] == c)
      return i;
  }
  return -1;
}
} // namespace

QString base32Encode(const QByteArray &data) {
  QString out;
  int buffer = 0;
  int bits = 0;
  for (unsigned char byte : data) {
    buffer = (buffer << 8) | byte;
    bits += 8;
    while (bits >= 5) {
      bits -= 5;
      out.append(QChar(kAlphabet[(buffer >> bits) & 0x1F]));
    }
  }
  if (bits > 0)
    out.append(QChar(kAlphabet[(buffer << (5 - bits)) & 0x1F]));
  return out;
}

QByteArray base32Decode(const QString &text) {
  QByteArray out;
  int buffer = 0;
  int bits = 0;
  for (QChar ch : text) {
    if (ch == '-' || ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r')
      continue;
    const int value = symbolValue(ch);
    if (value < 0)
      return QByteArray();
    buffer = (buffer << 5) | value;
    bits += 5;
    if (bits >= 8) {
      bits -= 8;
      out.append(static_cast<char>((buffer >> bits) & 0xFF));
    }
  }
  return out;
}

QString group(const QString &text, int groupSize) {
  if (groupSize <= 0)
    return text;
  QString out;
  for (int i = 0; i < text.size(); ++i) {
    if (i > 0 && i % groupSize == 0)
      out.append('-');
    out.append(text.at(i));
  }
  return out;
}

QByteArray encodeExpiryDays(quint32 expiryDays) {
  QByteArray out(kExpiryBytes, 0);
  out[0] = static_cast<char>((expiryDays >> 24) & 0xFF);
  out[1] = static_cast<char>((expiryDays >> 16) & 0xFF);
  out[2] = static_cast<char>((expiryDays >> 8) & 0xFF);
  out[3] = static_cast<char>(expiryDays & 0xFF);
  return out;
}

quint32 decodeExpiryDays(const QByteArray &fourBytes) {
  if (fourBytes.size() < kExpiryBytes)
    return 0;
  return (static_cast<quint32>(static_cast<unsigned char>(fourBytes[0])) << 24) |
         (static_cast<quint32>(static_cast<unsigned char>(fourBytes[1])) << 16) |
         (static_cast<quint32>(static_cast<unsigned char>(fourBytes[2])) << 8) |
         (static_cast<quint32>(static_cast<unsigned char>(fourBytes[3])));
}

QByteArray signedMessage(const QByteArray &fingerprint, quint32 expiryDays) {
  return fingerprint + encodeExpiryDays(expiryDays);
}

} // namespace license
