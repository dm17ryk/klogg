#include "cilogg_plugin.h"

int main()
{
    cilogg_plugin::Server plugin;
    plugin.action( "hello", [ &plugin ]( cilogg_plugin::Job& job, const QJsonObject& ) {
        plugin.panel( job, { { "id", "hello" },
                             { "title", "C++ Plugin" },
                             { "elements", QJsonArray{ QJsonObject{
                                               { "type", "text" },
                                               { "text", "Hello from another process!" } } } } } );
        return QJsonObject{ { "message", "Hello" } };
    } );
    plugin.serve();
}
