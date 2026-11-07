// license_gen_gui - the vendor-only license generator, with a simple GUI.
//
// Same job as the license_gen CLI, but point-and-click: paste a customer's
// Machine ID, click Generate, copy the key. NEVER shipped to customers - built
// only with -DCUTPRO_BUILD_LICENSE_TOOLS=ON and deployed into its own
// license-tools/ folder, apart from the app.
//
// The Ed25519 key material is loaded from a file the vendor keeps (default
// ~/.cutpro/license_priv.key), so the signing key is not baked into this exe.
#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QGuiApplication>
#include <QComboBox>
#include <QDate>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRandomGenerator>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

#include "app/license/license_codec.h"

extern "C" {
#include "ed25519.h"
}

using namespace license;

namespace {

// Default location of the vendor's private key file.
QString defaultKeyPath() {
  return QDir(QDir::homePath()).filePath(QStringLiteral(".cutpro/license_priv.key"));
}

// Load the 96-byte key material (pub || priv) from a hex file.
QByteArray loadKeyMaterial(const QString &path, QString *error) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    *error = QStringLiteral("Cannot open key file:\n%1").arg(path);
    return QByteArray();
  }
  const QByteArray key = QByteArray::fromHex(file.readAll().trimmed());
  if (key.size() != kPublicKeyBytes + 64) {
    *error = QStringLiteral("Key file must be %1 bytes (pub||priv); got %2.")
                 .arg(kPublicKeyBytes + 64)
                 .arg(key.size());
    return QByteArray();
  }
  return key;
}

// Sign a Machine ID with the given expiry (0 == perpetual), returning the
// CP2-... license key or an error.
QString signMachineId(const QString &machineId, const QByteArray &material,
                      quint32 expiryDays, QString *error) {
  const QByteArray fingerprint = base32Decode(machineId.trimmed());
  if (fingerprint.size() != kFingerprintBytes) {
    *error = QStringLiteral("Machine ID looks wrong: decoded to %1 bytes, "
                            "expected %2. Check for typos.")
                 .arg(fingerprint.size())
                 .arg(kFingerprintBytes);
    return QString();
  }
  const QByteArray pub = material.left(kPublicKeyBytes);
  const QByteArray priv = material.mid(kPublicKeyBytes);

  const QByteArray message = signedMessage(fingerprint, expiryDays);
  unsigned char signature[kSignatureBytes];
  ed25519_sign(signature,
               reinterpret_cast<const unsigned char *>(message.constData()),
               static_cast<size_t>(message.size()),
               reinterpret_cast<const unsigned char *>(pub.constData()),
               reinterpret_cast<const unsigned char *>(priv.constData()));
  const QByteArray sigBytes(reinterpret_cast<char *>(signature),
                            kSignatureBytes);
  const QByteArray payload = encodeExpiryDays(expiryDays) + sigBytes;
  return QString::fromLatin1(kKeyPrefix) + group(base32Encode(payload), 8);
}

} // namespace

int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setApplicationName(QStringLiteral("Cut Pro License Generator"));

  auto *window = new QWidget;
  window->setWindowTitle(QStringLiteral("Cut Pro - License Generator (vendor only)"));
  window->setMinimumWidth(560);

  auto *layout = new QVBoxLayout(window);
  layout->setContentsMargins(20, 20, 20, 20);
  layout->setSpacing(10);

  auto *title = new QLabel(QStringLiteral(
      "<b>Cut Pro License Generator</b><br>"
      "<span style='color:#a00'>Vendor only - do not share this tool.</span>"));
  layout->addWidget(title);

  // --- Private key file row ---
  layout->addWidget(new QLabel(QStringLiteral("Private key file:")));
  auto *keyRow = new QHBoxLayout;
  auto *keyEdit = new QLineEdit(defaultKeyPath());
  auto *browseBtn = new QPushButton(QStringLiteral("Browse..."));
  keyRow->addWidget(keyEdit);
  keyRow->addWidget(browseBtn);
  layout->addLayout(keyRow);

  // --- Machine ID row ---
  layout->addSpacing(6);
  layout->addWidget(new QLabel(QStringLiteral("Customer Machine ID:")));
  auto *machineEdit = new QLineEdit;
  machineEdit->setPlaceholderText(QStringLiteral("e.g. 7Q2M-9F4K-BR31-XT8N"));
  layout->addWidget(machineEdit);

  // --- Expiry row ---
  layout->addSpacing(6);
  layout->addWidget(new QLabel(QStringLiteral("License duration:")));
  auto *expiryRow = new QHBoxLayout;
  auto *durationSpin = new QSpinBox;
  durationSpin->setRange(1, 3650);
  durationSpin->setValue(1);
  auto *unitCombo = new QComboBox;
  unitCombo->addItems({QStringLiteral("Perpetual (never expires)"),
                       QStringLiteral("Days"), QStringLiteral("Months"),
                       QStringLiteral("Years")});
  auto *expiryPreview = new QLabel;
  expiryPreview->setStyleSheet(QStringLiteral("color:#666"));
  expiryRow->addWidget(durationSpin);
  expiryRow->addWidget(unitCombo);
  expiryRow->addWidget(expiryPreview, 1);
  layout->addLayout(expiryRow);

  // Compute the expiry day-count from the current unit/amount. 0 == perpetual.
  auto expiryDaysNow = [=]() -> quint32 {
    const int amount = durationSpin->value();
    QDate date;
    switch (unitCombo->currentIndex()) {
    case 1: date = QDate::currentDate().addDays(amount); break;
    case 2: date = QDate::currentDate().addMonths(amount); break;
    case 3: date = QDate::currentDate().addYears(amount); break;
    default: return 0; // perpetual
    }
    return static_cast<quint32>(QDate(1970, 1, 1).daysTo(date));
  };
  auto refreshExpiryPreview = [=]() {
    const bool perpetual = unitCombo->currentIndex() == 0;
    durationSpin->setEnabled(!perpetual);
    if (perpetual) {
      expiryPreview->setText(QStringLiteral("Never expires"));
    } else {
      const quint32 d = expiryDaysNow();
      expiryPreview->setText(QStringLiteral("Expires: %1").arg(
          QDate(1970, 1, 1).addDays(d).toString(Qt::ISODate)));
    }
  };
  QObject::connect(unitCombo, &QComboBox::currentIndexChanged, window,
                   [=](int) { refreshExpiryPreview(); });
  QObject::connect(durationSpin, &QSpinBox::valueChanged, window,
                   [=](int) { refreshExpiryPreview(); });
  refreshExpiryPreview();

  auto *generateBtn = new QPushButton(QStringLiteral("Generate License Key"));
  generateBtn->setDefault(true);
  layout->addWidget(generateBtn);

  // --- Output row ---
  layout->addSpacing(6);
  layout->addWidget(new QLabel(QStringLiteral("License key (give this to the customer):")));
  auto *outRow = new QHBoxLayout;
  auto *outEdit = new QLineEdit;
  outEdit->setReadOnly(true);
  auto *copyBtn = new QPushButton(QStringLiteral("Copy"));
  outRow->addWidget(outEdit);
  outRow->addWidget(copyBtn);
  layout->addLayout(outRow);

  auto *status = new QLabel;
  status->setWordWrap(true);
  layout->addWidget(status);

  layout->addStretch();

  // --- New keypair button (setup, rare) ---
  auto *keygenBtn = new QPushButton(QStringLiteral("Generate NEW keypair (advanced)..."));
  layout->addWidget(keygenBtn);

  // --- Wiring ---
  QObject::connect(browseBtn, &QPushButton::clicked, window, [=]() {
    const QString path = QFileDialog::getOpenFileName(
        window, QStringLiteral("Select private key file"), keyEdit->text());
    if (!path.isEmpty())
      keyEdit->setText(path);
  });

  QObject::connect(generateBtn, &QPushButton::clicked, window, [=]() {
    outEdit->clear();
    status->clear();
    QString error;
    const QByteArray material = loadKeyMaterial(keyEdit->text(), &error);
    if (material.isEmpty()) {
      status->setText(QStringLiteral("<span style='color:#a00'>%1</span>").arg(error));
      return;
    }
    const quint32 expiryDays = expiryDaysNow();
    const QString key = signMachineId(machineEdit->text(), material, expiryDays, &error);
    if (key.isEmpty()) {
      status->setText(QStringLiteral("<span style='color:#a00'>%1</span>").arg(error));
      return;
    }
    outEdit->setText(key);
    const QString when = expiryDays == 0
        ? QStringLiteral("perpetual")
        : QDate(1970, 1, 1).addDays(expiryDays).toString(Qt::ISODate);
    status->setText(QStringLiteral(
        "<span style='color:#080'>Key generated (%1). Click Copy and send it "
        "to the customer.</span>").arg(when));
  });

  QObject::connect(copyBtn, &QPushButton::clicked, window, [=]() {
    if (outEdit->text().isEmpty())
      return;
    QGuiApplication::clipboard()->setText(outEdit->text());
    status->setText(QStringLiteral("<span style='color:#080'>Copied to clipboard.</span>"));
  });

  QObject::connect(keygenBtn, &QPushButton::clicked, window, [=]() {
    const auto answer = QMessageBox::warning(
        window, QStringLiteral("Generate new keypair?"),
        QStringLiteral(
            "This creates a brand-new signing keypair.\n\n"
            "Every license key already issued will STOP working, and you must "
            "rebuild the app with the new public key. Only do this for initial "
            "setup or a full reset.\n\nContinue?"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes)
      return;

    unsigned char seed[32];
    auto *rng = QRandomGenerator::system();
    for (int i = 0; i < 32; ++i)
      seed[i] = static_cast<unsigned char>(rng->bounded(256));
    unsigned char pub[kPublicKeyBytes];
    unsigned char priv[64];
    ed25519_create_keypair(pub, priv, seed);
    const QByteArray pubBytes(reinterpret_cast<char *>(pub), kPublicKeyBytes);
    const QByteArray keyfile =
        pubBytes + QByteArray(reinterpret_cast<char *>(priv), 64);

    const QString savePath = QFileDialog::getSaveFileName(
        window, QStringLiteral("Save new private key file"), defaultKeyPath());
    if (savePath.isEmpty())
      return;
    QFile file(savePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
      QMessageBox::critical(window, QStringLiteral("Error"),
                            QStringLiteral("Could not write %1").arg(savePath));
      return;
    }
    file.write(keyfile.toHex());
    file.close();
    keyEdit->setText(savePath);

    // Build the obfuscated public-key snippet for license_pubkey.cpp.
    QByteArray mask(kPublicKeyBytes, 0);
    for (int i = 0; i < kPublicKeyBytes; ++i)
      mask[i] = static_cast<char>(rng->bounded(256));
    QString snippet =
        QStringLiteral("// --- BEGIN GENERATED PUBLIC KEY ---\n"
                       "constexpr unsigned char maskedKey[kPublicKeyBytes] = {\n    ");
    for (int i = 0; i < kPublicKeyBytes; ++i)
      snippet += QString::asprintf(
          "0x%02X, ", static_cast<unsigned char>(pubBytes[i] ^ mask[i]));
    snippet += QStringLiteral("\n};\nconstexpr unsigned char mask[kPublicKeyBytes] = {\n    ");
    for (int i = 0; i < kPublicKeyBytes; ++i)
      snippet += QString::asprintf("0x%02X, ", static_cast<unsigned char>(mask[i]));
    snippet += QStringLiteral("\n};\n// --- END GENERATED PUBLIC KEY ---");

    QMessageBox box(window);
    box.setWindowTitle(QStringLiteral("New keypair created"));
    box.setText(QStringLiteral(
        "Private key saved to:\n%1\n\nPaste the snippet below into "
        "src/app/license/license_pubkey.cpp, then rebuild the app.").arg(savePath));
    box.setDetailedText(snippet);
    box.exec();
  });

  window->show();
  return app.exec();
}
