#include "ui/dialogs/AboutDialog.h"

#include "app/Config.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include <array>

namespace notes::ui {

namespace {

struct License
{
    const char* component;
    const char* file;
};

// Everything under resources/licenses/.
constexpr std::array kLicenses = {
    License{.component = "Inter (font)", .file = ":/licenses/Inter-OFL.txt"},
    License{.component = "JetBrains Mono (font)", .file = ":/licenses/JetBrainsMono-OFL.txt"},
    License{.component = "Lucide (icons)", .file = ":/licenses/Lucide-ISC.txt"},
};

QString licenseText()
{
    QString text;
    for (const License& license : kLicenses) {
        QFile file(QString::fromLatin1(license.file));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            continue;
        }
        text += QStringLiteral("%1\n%2\n\n%3\n\n")
                    .arg(QString::fromLatin1(license.component),
                         QString(static_cast<int>(qstrlen(license.component)), u'='),
                         QString::fromUtf8(file.readAll()).trimmed());
    }
    return text;
}

} // namespace

AboutDialog::AboutDialog(QWidget* parent)
    : QDialog(parent)
{
    const QString displayName = QString::fromLatin1(config::kDisplayName);
    setWindowTitle(tr("About %1").arg(displayName));

    auto* title = new QLabel(QStringLiteral("<b>%1</b> %2")
                                 .arg(displayName.toHtmlEscaped(),
                                      QString::fromLatin1(config::kAppVersion).toHtmlEscaped()),
                             this);
    auto* qtNotice = new QLabel(tr("Built with Qt %1, used under the GNU LGPL version 3.")
                                    .arg(QString::fromLatin1(qVersion())),
                                this);

    auto* licensesLabel = new QLabel(tr("Licenses"), this);
    auto* licenses = new QPlainTextEdit(licenseText(), this);
    licenses->setObjectName(QStringLiteral("licenses"));
    licenses->setReadOnly(true);
    licensesLabel->setBuddy(licenses);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    const QPushButton* aboutQt = buttons->addButton(tr("About Qt"), QDialogButtonBox::HelpRole);
    connect(aboutQt, &QPushButton::clicked, &QApplication::aboutQt);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(title);
    layout->addWidget(qtNotice);
    layout->addSpacing(8);
    layout->addWidget(licensesLabel);
    layout->addWidget(licenses, 1);
    layout->addWidget(buttons);

    resize(560, 480);
}

} // namespace notes::ui
