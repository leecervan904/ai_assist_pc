#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <memory>

class InferenceEngine : public QObject
{
    Q_OBJECT

public:
    explicit InferenceEngine(QString modelPath, QObject *parent = nullptr);
    ~InferenceEngine() override;

public slots:
    void initialize();
    void load(const QString &modelPath);
    void process(const QImage &frame);

signals:
    void resultReady(const QVariantMap &result);

private:
    struct Impl;
    QString m_modelPath;
    std::unique_ptr<Impl> m_impl;
};
