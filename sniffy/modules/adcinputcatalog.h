#ifndef ADCINPUTCATALOG_H
#define ADCINPUTCATALOG_H

#include <QByteArray>
#include <QStringList>
#include <QVector>

// Shared INP_ metadata; preserve all inputs and let each instrument filter its choices.
struct AdcInputCatalog
{
    QVector<QStringList> inputs;
    QVector<int> defaults;

    bool parse(QByteArray response, QString &error);
    static bool isExternal(const QString &name);
};

#endif // ADCINPUTCATALOG_H
