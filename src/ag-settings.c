#include "ag-settings.h"

#define SETTINGS_SCHEMA_ID_WINDOW "eu.polonkai.gergely.Astrognome.state.window"
#define SETTINGS_SCHEMA_ID_CHART "eu.polonkai.gergely.Astrognome.state.chart"

static AgSettings *singleton = NULL;

struct _AgSettingsPrivate {
    GSettings *settings_window;
    GSettings *settings_chart;
};

G_DEFINE_TYPE_WITH_PRIVATE(AgSettings, ag_settings, G_TYPE_OBJECT);

static void
ag_settings_init(AgSettings *settings)
{
    settings->priv                  = ag_settings_get_instance_private(settings);
    settings->priv->settings_window = g_settings_new(SETTINGS_SCHEMA_ID_WINDOW);
    settings->priv->settings_chart  = g_settings_new(SETTINGS_SCHEMA_ID_CHART);
}

static void
ag_settings_dispose(GObject *object)
{
    AgSettings *settings = AG_SETTINGS(object);

    g_clear_object(&settings->priv->settings_window);
    g_clear_object(&settings->priv->settings_chart);

    G_OBJECT_CLASS(ag_settings_parent_class)->dispose(object);
}

static void
ag_settings_finalize(GObject *object)
{
    singleton = NULL;
    G_OBJECT_CLASS(ag_settings_parent_class)->finalize(object);
}

static void
ag_settings_class_init(AgSettingsClass *klass)
{
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);

    gobject_class->dispose  = ag_settings_dispose;
    gobject_class->finalize = ag_settings_finalize;
}

AgSettings *
ag_settings_get(void)
{
    if (!singleton) {
        singleton = AG_SETTINGS(g_object_new(AG_TYPE_SETTINGS, NULL));
    } else {
        g_object_ref(singleton);
    }

    g_assert(singleton);

    return singleton;
}

GSettings *
ag_settings_peek_window_settings(AgSettings *settings)
{
    return settings->priv->settings_window;
}

GSettings *
ag_settings_peek_chart_settings(AgSettings *settings)
{
    return settings->priv->settings_chart;
}

