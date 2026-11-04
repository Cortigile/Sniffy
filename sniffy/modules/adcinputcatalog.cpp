#include "adcinputcatalog.h"

bool AdcInputCatalog::parse(QByteArray response, QString &error)
{
    error.clear();
    if (response.startsWith("INP_"))
        response.remove(0, 4);
    if (response.endsWith(';'))
        response.chop(1);
    const int slotCount = response.indexOf('/');
    if (slotCount < 1 || slotCount > 4) {
        error = QStringLiteral("Invalid ADC input catalog slot header.");
        return false;
    }
    AdcInputCatalog parsed;
    int position = slotCount;
    for (int slot = 0; slot < slotCount; ++slot) {
        const int end = response.indexOf('/', position + 1);
        if (position >= response.size() || response.at(position) != '/' || end < 0) {
            error = QStringLiteral("Incomplete ADC input catalog.");
            return false;
        }
        const auto names = response.mid(position + 1, end - position - 1).split(':');
        const int selector = static_cast<quint8>(response.at(slot));
        if (names.isEmpty() || names.size() > 256 || selector >= names.size()) {
            error = QStringLiteral("Invalid default ADC input selector.");
            return false;
        }
        QStringList options;
        for (const auto &name : names) {
            if (name.isEmpty()) {
                error = QStringLiteral("Empty ADC input name.");
                return false;
            }
            options.append(QString::fromLatin1(name));
        }
        parsed.inputs.append(options);
        parsed.defaults.append(selector);
        position = end;
    }
    if (position != response.size() - 1) {
        error = QStringLiteral("Unexpected trailing ADC input catalog data.");
        return false;
    }
    *this = parsed;
    return true;
}

bool AdcInputCatalog::isExternal(const QString &name)
{
    const auto normalized = name.trimmed().toLower();
    return !normalized.isEmpty() && normalized != QLatin1String("temp")
        && normalized != QLatin1String("temperature") && normalized != QLatin1String("vref")
        && normalized != QLatin1String("vbat");
}
