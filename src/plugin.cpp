/*
    Qt6 QML plugin registration for dropdown terminal widget.
*/

#include <QtQml/QQmlExtensionPlugin>
#include <QtQml/qqml.h>
#include "textrender.h"

class DroptermPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)

public:
    void registerTypes(const char *uri) override
    {
        Q_ASSERT(QLatin1String(uri) == QLatin1String("dropterm"));
        qmlRegisterType<TextRender>(uri, 1, 0, "TextRender");
    }
};

#include "plugin.moc"
