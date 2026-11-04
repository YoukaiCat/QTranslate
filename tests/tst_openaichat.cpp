#include "ai/openaichat.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

class TestOpenAiChat : public QObject
{
    Q_OBJECT

private slots:
    void serializesChatRequest()
    {
        OpenAiChat::Request request;
        request.endpoint = QUrl(QStringLiteral("http://127.0.0.1:1234/v1/chat/completions"));
        request.model = QStringLiteral("test/model");
        request.messages = {
            {QStringLiteral("system"), QStringLiteral("Translate it.")},
            {QStringLiteral("user"), QStringLiteral("Hello")},
        };
        request.temperature = 0.1;

        const QJsonObject body =
            QJsonDocument::fromJson(OpenAiChat::serialize(request)).object();
        QCOMPARE(body.value(QStringLiteral("model")).toString(), QStringLiteral("test/model"));
        QCOMPARE(body.value(QStringLiteral("temperature")).toDouble(), 0.1);
        QCOMPARE(body.value(QStringLiteral("stream")).toBool(), false);
        QVERIFY(!body.contains(QStringLiteral("max_tokens")));
        const QJsonArray messages = body.value(QStringLiteral("messages")).toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages.at(0).toObject().value(QStringLiteral("role")).toString(),
                 QStringLiteral("system"));
        QCOMPARE(messages.at(1).toObject().value(QStringLiteral("content")).toString(),
                 QStringLiteral("Hello"));
    }

    void serializesMaxTokens()
    {
        OpenAiChat::Request request;
        request.model = QStringLiteral("test/model");
        request.maxTokens = 512;
        const QJsonObject body =
            QJsonDocument::fromJson(OpenAiChat::serialize(request)).object();
        QCOMPARE(body.value(QStringLiteral("max_tokens")).toInt(), 512);
    }

    void parsesStringContent()
    {
        const QByteArray body = R"({
            "model": "test/model",
            "choices": [{
                "message": {"role": "assistant", "content": "Hallo, Welt"},
                "finish_reason": "stop"
            }],
            "usage": {"prompt_tokens": 12, "completion_tokens": 3}
        })";

        const OpenAiChat::Response response = OpenAiChat::parse(body);
        QVERIFY(response.ok());
        QCOMPARE(response.text, QStringLiteral("Hallo, Welt"));
        QCOMPARE(response.model, QStringLiteral("test/model"));
        QCOMPARE(response.finishReason, QStringLiteral("stop"));
        QCOMPARE(response.promptTokens, 12);
        QCOMPARE(response.completionTokens, 3);
    }

    void parsesContentParts()
    {
        const QByteArray body = R"({
            "choices": [{
                "message": {"content": [
                    {"type": "text", "text": "Hallo "},
                    {"type": "text", "text": "Welt"}
                ]},
                "finish_reason": "stop"
            }]
        })";

        const OpenAiChat::Response response = OpenAiChat::parse(body);
        QVERIFY(response.ok());
        QCOMPARE(response.text, QStringLiteral("Hallo Welt"));
    }

    void rejectsEmptyContent()
    {
        const QByteArray body = R"({"choices":[{"message":{"content":""},"finish_reason":"stop"}]})";
        const OpenAiChat::Response response = OpenAiChat::parse(body);
        QCOMPARE(response.error, TranslationError::BadResponse);
        QVERIFY(!response.errorMessage.isEmpty());
    }

    void rejectsMalformedResponse()
    {
        const OpenAiChat::Response response = OpenAiChat::parse("<html>error</html>");
        QCOMPARE(response.error, TranslationError::BadResponse);
    }

    void mapsFinishLengthToTooLong()
    {
        const QByteArray body = R"({"choices":[{"message":{"content":"partial"},"finish_reason":"length"}]})";
        const OpenAiChat::Response response = OpenAiChat::parse(body);
        QCOMPARE(response.error, TranslationError::TooLong);
    }

    void mapsErrorObject()
    {
        const QByteArray body =
            R"({"error":{"code":429,"message":"Rate limited"}})";
        const OpenAiChat::Response response = OpenAiChat::parse(body);
        QCOMPARE(response.error, TranslationError::RateLimited);
        QCOMPARE(response.errorCode, 429);
    }

    void mapsHttpStatuses()
    {
        QCOMPARE(OpenAiChat::errorForHttpStatus(401), TranslationError::Auth);
        QCOMPARE(OpenAiChat::errorForHttpStatus(403), TranslationError::Auth);
        QCOMPARE(OpenAiChat::errorForHttpStatus(402), TranslationError::QuotaExceeded);
        QCOMPARE(OpenAiChat::errorForHttpStatus(408), TranslationError::Timeout);
        QCOMPARE(OpenAiChat::errorForHttpStatus(413), TranslationError::TooLong);
        QCOMPARE(OpenAiChat::errorForHttpStatus(429), TranslationError::RateLimited);
        QCOMPARE(OpenAiChat::errorForHttpStatus(500), TranslationError::Network);
        QCOMPARE(OpenAiChat::errorForHttpStatus(400), TranslationError::BadResponse);
    }

    void parsesModelList()
    {
        const QByteArray body = R"({"data":[
            {"id":"openai/gpt-4o-mini"},
            {"id":"deepseek/deepseek-chat"},
            {"id":"openai/gpt-4o-mini"}
        ]})";
        QString error;
        const QStringList models = OpenAiChat::parseModels(body, &error);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(models,
                 (QStringList{QStringLiteral("deepseek/deepseek-chat"),
                              QStringLiteral("openai/gpt-4o-mini")}));
    }

    void rejectsEmptyModelList()
    {
        QString error;
        QVERIFY(OpenAiChat::parseModels(R"({"data":[]})", &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void buildsEndpoints()
    {
        QCOMPARE(OpenAiChat::chatCompletionsUrl(QStringLiteral("https://openrouter.ai/api/v1")),
                 QUrl(QStringLiteral("https://openrouter.ai/api/v1/chat/completions")));
        QCOMPARE(OpenAiChat::modelsUrl(QStringLiteral("https://openrouter.ai/api/v1")),
                 QUrl(QStringLiteral("https://openrouter.ai/api/v1/models")));
        QCOMPARE(OpenAiChat::chatCompletionsUrl(QStringLiteral("http://127.0.0.1:11434")),
                 QUrl(QStringLiteral("http://127.0.0.1:11434/v1/chat/completions")));
        QCOMPARE(OpenAiChat::chatCompletionsUrl(
                     QStringLiteral("http://127.0.0.1:8080/v1/chat/completions")),
                 QUrl(QStringLiteral("http://127.0.0.1:8080/v1/chat/completions")));
        QVERIFY(OpenAiChat::chatCompletionsUrl(QString()).isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestOpenAiChat)
#include "tst_openaichat.moc"
