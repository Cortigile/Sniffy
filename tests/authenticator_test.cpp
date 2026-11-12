#include <QtTest>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonObject>

#include "auth/authenticator.h"

namespace {
class TokenReply final : public QNetworkReply
{
public:
    TokenReply(const QJsonObject &response, QObject *parent)
        : QNetworkReply(parent)
    {
        body.setData(QJsonDocument(response).toJson(QJsonDocument::Compact));
        body.open(QIODevice::ReadOnly);
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, 200);
        open(QIODevice::ReadOnly);
        setFinished(true);
    }

    void abort() override {}
    qint64 bytesAvailable() const override
    {
        return body.bytesAvailable() + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        return body.read(data, maxSize);
    }

private:
    QBuffer body;
};
}

class AuthenticatorTest : public QObject
{
    Q_OBJECT

private slots:
    void tokenRefreshReconnect_data()
    {
        QTest::addColumn<bool>("demo");
        QTest::addColumn<QString>("license");
        QTest::addColumn<bool>("manual");
        QTest::addColumn<bool>("reconnect");

        QTest::newRow("paid-to-demo") << false << QString("DE") << false << true;
        QTest::newRow("demo-to-full") << true << QString("FU") << false << true;
        QTest::newRow("demo-to-extended") << true << QString("EX") << false << true;
        QTest::newRow("demo-unchanged") << true << QString("DE") << false << false;
        QTest::newRow("full-unchanged") << false << QString("FU") << false << false;
        QTest::newRow("extended-unchanged") << false << QString("EX") << false << false;
        QTest::newRow("legacy-demo") << true << QString() << false << false;
        QTest::newRow("legacy-paid") << false << QString() << false << false;
        QTest::newRow("manual-demo") << true << QString("DE") << true << true;
        QTest::newRow("manual-full") << false << QString("FU") << true << true;
        QTest::newRow("manual-downgrade") << false << QString("DE") << true << true;
    }

    void tokenRefreshReconnect()
    {
        QFETCH(bool, demo);
        QFETCH(QString, license);
        QFETCH(bool, manual);
        QFETCH(bool, reconnect);

        Authenticator authenticator;
        authenticator.setDemoMode(demo);
        authenticator.authenticationSentManual = manual;
        QSignalSpy success(&authenticator, &Authenticator::authenticationSucceeded);
        QSignalSpy failure(&authenticator, &Authenticator::authenticationFailed);
        const QString validTill = QDate::currentDate().addDays(7).toString(Qt::ISODate);
        const QByteArray token(384, 'a');
        QJsonObject response{
            {"valid_till", validTill},
            {"token", QString::fromLatin1(token)},
            {"context", manual ? "login" : "extension"}
        };
        if (!license.isEmpty())
            response.insert("license", license);

        auto *reply = new TokenReply(response, &authenticator);
        authenticator.currentReply = reply;
        authenticator.onFinished(reply);

        QCOMPARE(failure.count(), 0);
        QCOMPARE(success.count(), 1);
        QCOMPARE(success.at(0).at(0).toDateTime().date().toString(Qt::ISODate), validTill);
        QCOMPARE(success.at(0).at(1).toByteArray(), token);
        QCOMPARE(success.at(0).at(2).toBool(), reconnect);
        QVERIFY(!authenticator.isManualAuth());
    }
};

QTEST_GUILESS_MAIN(AuthenticatorTest)
#include "authenticator_test.moc"
