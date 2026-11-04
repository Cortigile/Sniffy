#include <QtTest>
#include "modules/patterngenerator/patterngeneratorwindow.h"

class PatternGeneratorTest : public QObject
{
    Q_OBJECT

    static QList<patttype> defaultUserDefined(int length)
    {
        QList<patttype> result;
        for (int i = 0; i < length; ++i)
            result.append(CLOCK_CH8 & CLOCK_MASK(i));
        return result;
    }

private slots:
    void initTestCase()
    {
        Graphics::setTheme(0);
    }

    void lengthChangesPreserveAllChannelEdits()
    {
        PatternGeneratorPatterns patterns;
        auto *data = patterns.createPatternOrJustGetData(0, 8);
        QCOMPARE(*data, defaultUserDefined(8));
        for (int channel = 0; channel < PATT_MAX_CHANNELS_NUM; ++channel)
            patterns.modifyPattern(channel, channel);
        const auto edited = *data;

        QCOMPARE(patterns.setDataLen(0, 8), data);
        QCOMPARE(*data, edited);
        patterns.setDataLen(0, 13);
        QCOMPARE(*data, edited + defaultUserDefined(13).mid(8));
        patterns.modifyPattern(2, 12);
        const auto extended = *data;

        patterns.setDataLen(0, 2);
        QCOMPARE(*data, extended.first(2));
        patterns.modifyPattern(1, 0);
        auto expected = extended;
        expected[0] ^= 1 << 1;
        patterns.setDataLen(0, 7);
        QCOMPARE(*data, expected.first(7));
        patterns.setDataLen(0, 100);
        QCOMPARE(*data, expected + defaultUserDefined(100).mid(13));

        patterns.setDataLen(0, 3);
        patterns.createPatternOrJustGetData(1, 8);
        QCOMPARE(patterns.createPatternOrJustGetData(0, 3), data);
        patterns.setDataLen(0, 13);
        QCOMPARE(*data, expected);
    }

    void resetClearsVisibleAndHiddenEdits()
    {
        PatternGeneratorPatterns patterns;
        auto *data = patterns.createPatternOrJustGetData(0, 23);
        patterns.modifyPattern(0, 0);
        patterns.modifyPattern(7, 22);
        patterns.setDataLen(0, 7);

        QCOMPARE(patterns.setDefault(0), data);
        QCOMPARE(*data, defaultUserDefined(7));
        patterns.setDataLen(0, 23);
        QCOMPARE(*data, defaultUserDefined(23));
        patterns.setDefault(0);
        QCOMPARE(*data, defaultUserDefined(23));
    }

    void resetBeforeFirstSelectionUsesDefaultLength()
    {
        PatternGeneratorPatterns patterns;
        QCOMPARE(*patterns.setDefault(0), defaultUserDefined(PATT_DEFAULT_DATA_LENGTH));
        patterns.modifyPattern(0, 0);
        QCOMPARE(patterns.createPatternOrJustGetData(0, PATT_DEFAULT_DATA_LENGTH)->at(0),
                 patttype(CLOCK_CH8 | 1));
    }

    void generatedPatternsStillRegenerate_data()
    {
        QTest::addColumn<int>("index");
        QTest::addColumn<int>("parameter");
        QTest::newRow("counter") << 1 << 6;
        QTest::newRow("binary") << 2 << 4;
        QTest::newRow("gray") << 3 << 3;
    }

    void generatedPatternsStillRegenerate()
    {
        QFETCH(int, index);
        QFETCH(int, parameter);
        PatternGeneratorPatterns patterns;
        patterns.createPatternOrJustGetData(index, PATT_DEFAULT_DATA_LENGTH);
        patterns.modifyPattern(0, 0);

        PatternGeneratorPatterns reference;
        const auto expected = *reference.createPatternOrJustGetData(index, parameter);
        QCOMPARE(*patterns.setDataLen(index, parameter), expected);
        const auto defaults = *reference.setDefault(index);
        QCOMPARE(*patterns.setDefault(index), defaults);
    }

    void userDefinedControlsPreserveLengthOnReset_data()
    {
        QTest::addColumn<int>("length");
        QTest::newRow("minimum") << 2;
        QTest::newRow("odd") << 7;
        QTest::newRow("extended") << 23;
        QTest::newRow("maximum") << 100;
    }

    void userDefinedControlsPreserveLengthOnReset()
    {
        QFETCH(int, length);
        PatternGeneratorConfig config;
        PatternGeneratorWindow window(&config);
        window.restoreGUIAfterStartup();
        auto *settings = window.settings;
        auto *data = window.getPatternData();
        settings->dialUserDefLength->setRealValue(100);
        window.patterns->modifyPattern(0, 0);
        window.patterns->modifyPattern(7, 99);
        const auto edited = *data;
        settings->dialUserDefLength->setRealValue(length);
        QCOMPARE(*window.getPatternData(), edited.first(length));
        QCOMPARE(config.dataLen[0], float(length));

        settings->dialUserDefFreq->setRealValue(2000);
        settings->buttonSetDefault->clicked(0);
        QCOMPARE(settings->dialUserDefLength->getRealValue(), float(length));
        QCOMPARE(config.dataLen[0], float(length));
        QCOMPARE(*window.getPatternData(), defaultUserDefined(length));
        QCOMPARE(settings->dialUserDefFreq->getRealValue(), float(PATT_DEFAULT_GEN_FREQ));
        QCOMPARE(config.freq[0], qreal(PATT_DEFAULT_GEN_FREQ));

        settings->dialUserDefLength->setRealValue(100);
        QCOMPARE(*window.getPatternData(), defaultUserDefined(100));
        settings->comboPatternSelection->setSelected(1);
        settings->comboPatternSelection->setSelected(0);
        QCOMPARE(window.getPatternData(), data);
        QCOMPARE(config.dataLen[0], 100.0f);
        QCOMPARE(data->size(), 100);
    }
};

QTEST_MAIN(PatternGeneratorTest)
#include "patterngenerator_test.moc"
