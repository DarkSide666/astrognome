#include <math.h>
#include <string.h>
#include <glib/gi18n.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <webkit/webkit.h>

#include <swe-glib.h>

#include "ag-app.h"
#include "ag-window.h"
#include "ag-chart.h"
#include "ag-settings.h"

struct _AgWindowPrivate {
    GtkWidget     *header_bar;
    GtkWidget     *stack;
    GtkWidget     *name;
    GtkWidget     *north_lat;
    GtkWidget     *south_lat;
    GtkWidget     *latitude;
    GtkWidget     *east_long;
    GtkWidget     *west_long;
    GtkWidget     *longitude;
    GtkWidget     *year;
    GtkWidget     *month;
    GtkWidget     *day;
    GtkWidget     *hour;
    GtkWidget     *minute;
    GtkWidget     *second;
    GtkWidget     *timezone;

    GtkWidget     *tab_edit;
    GtkWidget     *current_tab;

    GtkWidget     *aspect_table;
    GtkWidget     *chart_web_view;
    GtkAdjustment *year_adjust;

    AgSettings    *settings;
    AgChart       *chart;
    gchar         *uri;
    gboolean      aspect_table_populated;
};

G_DEFINE_QUARK(ag_window_error_quark, ag_window_error);

G_DEFINE_TYPE_WITH_PRIVATE(AgWindow, ag_window, GTK_TYPE_APPLICATION_WINDOW);

static void recalculate_chart(AgWindow *window);

static void
ag_window_gear_menu_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    GVariant *state;

    state = g_action_get_state(G_ACTION(action));
    g_action_change_state(G_ACTION(action), g_variant_new_boolean(!g_variant_get_boolean(state)));

    g_variant_unref(state);
}

static void
ag_window_view_menu_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    GVariant *state;

    state = g_action_get_state(G_ACTION(action));
    g_action_change_state(G_ACTION(action), g_variant_new_boolean(!g_variant_get_boolean(state)));

    g_variant_unref(state);
}

static void
ag_window_close_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    AgWindow *window = user_data;

    // TODO: Save unsaved changes!
    gtk_widget_destroy(GTK_WIDGET(window));
}

static void
ag_window_save_as(AgWindow *window, GError **err)
{
    gchar           *name;
    gchar           *file_name;
    GtkWidget       *fs;
    gint            response;
    AgWindowPrivate *priv = ag_window_get_instance_private(window);

    recalculate_chart(window);

    // We should never enter here, but who knows...
    if (priv->chart == NULL) {
        GtkWidget *dialog;

        dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("Chart cannot be calculated."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_set_error(err, AG_WINDOW_ERROR, AG_WINDOW_ERROR_EMPTY_CHART, "Chart is empty");

        return;
    }

    name = ag_chart_get_name(priv->chart);

    if ((name == NULL) || (*name == 0)) {
        GtkWidget *dialog;

        g_free(name);

        dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("You must enter a name before saving a chart."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_set_error(err, AG_WINDOW_ERROR, AG_WINDOW_ERROR_NO_NAME, "No name specified");

        return;
    }

    file_name = g_strdup_printf("%s.agc", name);
    g_free(name);

    fs = gtk_file_chooser_dialog_new(_("Save Chart"),
                                     GTK_WINDOW(window),
                                     GTK_FILE_CHOOSER_ACTION_SAVE,
                                     _("_Cancel"), GTK_RESPONSE_CANCEL,
                                     _("_Save"), GTK_RESPONSE_ACCEPT,
                                     NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(fs), GTK_RESPONSE_ACCEPT);
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(fs), FALSE);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(fs), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fs), file_name);
    g_free(file_name);

    response = gtk_dialog_run(GTK_DIALOG(fs));
    gtk_widget_hide(fs);

    if (response == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(fs));

        ag_chart_save_to_file(priv->chart, file, err);
    }

    gtk_widget_destroy(fs);
}

static void
ag_window_save_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    gchar           *uri;
    AgWindow        *window = AG_WINDOW(user_data);
    GError          *err    = NULL;
    AgWindowPrivate *priv   = ag_window_get_instance_private(window);

    recalculate_chart(window);
    uri = ag_window_get_uri(window);

    if (uri != NULL) {
        GFile *file = g_file_new_for_uri(uri);
        g_free(uri);

        ag_chart_save_to_file(priv->chart, file, &err);
    } else {
        ag_window_save_as(window, &err);
    }

    if (err) {
        ag_app_message_dialog(GTK_WIDGET(window), GTK_MESSAGE_ERROR, "%s", err->message);
    }
}

static void
ag_window_save_as_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    AgWindow *window = AG_WINDOW(user_data);
    GError   *err    = NULL;

    recalculate_chart(window);
    ag_window_save_as(window, &err);

    if (err) {
        ag_app_message_dialog(GTK_WIDGET(window), GTK_MESSAGE_ERROR, "%s", err->message);
    }
}

static void
ag_window_export_svg(AgWindow *window, GError **err)
{
    gchar           *name;
    gchar           *file_name;
    GtkWidget       *fs;
    gint            response;
    AgWindowPrivate *priv = ag_window_get_instance_private(window);

    recalculate_chart(window);

    // We should never enter here, but who knows...
    if (priv->chart == NULL) {
        GtkWidget *dialog;

        dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("Chart cannot be calculated."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_set_error(err, AG_WINDOW_ERROR, AG_WINDOW_ERROR_EMPTY_CHART, "Chart is empty");

        return;
    }

    name = ag_chart_get_name(priv->chart);

    if ((name == NULL) || (*name == 0)) {
        GtkWidget *dialog;

        g_free(name);

        dialog = gtk_message_dialog_new(GTK_WINDOW(window), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK, _("You must enter a name before saving a chart."));
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_set_error(err, AG_WINDOW_ERROR, AG_WINDOW_ERROR_NO_NAME, "No name specified");

        return;
    }

    file_name = g_strdup_printf("%s.svg", name);
    g_free(name);

    fs = gtk_file_chooser_dialog_new(_("Export Chart as SVG"),
                                     GTK_WINDOW(window),
                                     GTK_FILE_CHOOSER_ACTION_SAVE,
                                     _("_Cancel"), GTK_RESPONSE_CANCEL,
                                     _("_Save"), GTK_RESPONSE_ACCEPT,
                                     NULL);
    gtk_dialog_set_default_response(GTK_DIALOG(fs), GTK_RESPONSE_ACCEPT);
    gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(fs), FALSE);
    gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(fs), TRUE);
    gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(fs), file_name);
    g_free(file_name);

    response = gtk_dialog_run(GTK_DIALOG(fs));
    gtk_widget_hide(fs);

    if (response == GTK_RESPONSE_ACCEPT) {
        GFile *file = gtk_file_chooser_get_file(GTK_FILE_CHOOSER(fs));

        ag_chart_export_svg_to_file(priv->chart, file, err);
    }

    gtk_widget_destroy(fs);
}

static void
ag_window_export_svg_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    AgWindow *window = AG_WINDOW(user_data);
    GError *err = NULL;

    ag_window_export_svg(window, &err);

    if (err) {
        ag_app_message_dialog(GTK_WIDGET(window), GTK_MESSAGE_ERROR, "%s", err->message);
    }
}

const gchar *
ag_window_planet_character(GswePlanet planet)
{
    switch (planet) {
        case GSWE_PLANET_ASCENDANT:
            return "AC";

        case GSWE_PLANET_MC:
            return "MC";

        case GSWE_PLANET_VERTEX:
            return "Vx";

        case GSWE_PLANET_SUN:
            return "☉";

        case GSWE_PLANET_MOON:
            return "☽";

        case GSWE_PLANET_MOON_NODE:
            return "☊";

        case GSWE_PLANET_MERCURY:
            return "☿";

        case GSWE_PLANET_VENUS:
            return "♀";

        case GSWE_PLANET_MARS:
            return "♂";

        case GSWE_PLANET_JUPITER:
            return "♃";

        case GSWE_PLANET_SATURN:
            return "♄";

        case GSWE_PLANET_URANUS:
            return "♅";

        case GSWE_PLANET_NEPTUNE:
            return "♆";

        case GSWE_PLANET_PLUTO:
            return "♇";

        case GSWE_PLANET_CERES:
            return "⚳";

        case GSWE_PLANET_PALLAS:
            return "⚴";

        case GSWE_PLANET_JUNO:
            return "⚵";

        case GSWE_PLANET_VESTA:
            return "⚶";

        case GSWE_PLANET_CHIRON:
            return "⚷";

        case GSWE_PLANET_MOON_APOGEE:
            return "⚸";

        default:
            return NULL;
    }
}

GtkWidget *
ag_window_create_planet_widget(GswePlanetInfo *planet_info)
{
    const gchar *planet_char;
    GswePlanet  planet    = gswe_planet_info_get_planet(planet_info);
    GSettings   *settings = ag_settings_peek_main_settings(ag_settings_get());

    if (
            ((planet_char = ag_window_planet_character(planet)) != NULL)
            && (g_settings_get_boolean(settings, "planets-char"))
        )
    {
        return gtk_label_new(planet_char);
    }

    switch (planet) {
        case GSWE_PLANET_SUN:
            return gtk_image_new_from_resource("/eu/polonkai/gergely/Astrognome/default-icons/planet-sun.svg");

        default:
            return gtk_label_new(gswe_planet_info_get_name(planet_info));
    }
}

const gchar *
ag_window_aspect_character(GsweAspect aspect)
{
    switch (aspect) {
        case GSWE_ASPECT_CONJUCTION:
            return "☌";

        case GSWE_ASPECT_OPPOSITION:
            return "☍";

        case GSWE_ASPECT_QUINTILE:
            return "Q";

        case GSWE_ASPECT_BIQUINTILE:
            return "BQ";

        case GSWE_ASPECT_SQUARE:
            return "◽";

        case GSWE_ASPECT_TRINE:
            return "▵";

        case GSWE_ASPECT_SEXTILE:
            return "⚹";

        case GSWE_ASPECT_SEMISEXTILE:
            return "⚺";

        case GSWE_ASPECT_QUINCUNX:
            return "⚻";

        case GSWE_ASPECT_SESQUISQUARE:
            return "⚼";

        default:
            return NULL;
    }
}

GtkWidget *
ag_window_create_aspect_widget(GsweAspectInfo *aspect_info)
{
    const gchar *aspect_char;
    GsweAspect  aspect    = gswe_aspect_info_get_aspect(aspect_info);
    GSettings   *settings = ag_settings_peek_main_settings(ag_settings_get());

    if (
            ((aspect_char = ag_window_aspect_character(aspect)) != NULL)
            && (g_settings_get_boolean(settings, "aspects-char"))
        )
    {
        return gtk_label_new(aspect_char);
    }

    switch (aspect) {
        default:
            return gtk_label_new(gswe_aspect_info_get_name(aspect_info));
    }
}

void
ag_window_redraw_aspect_table(AgWindow *window)
{
    GList           *planet_list,
                    *planet1,
                    *planet2;
    guint           i,
                    j;
    AgWindowPrivate *priv        = ag_window_get_instance_private(window);

    planet_list = ag_chart_get_planets(priv->chart);

    if (priv->aspect_table_populated == FALSE) {
        GList *planet;
        guint i;

        for (planet = planet_list, i = 0; planet; planet = g_list_next(planet), i++) {
            GtkWidget      *label_hor,
                           *label_ver,
                           *current_widget;
            GswePlanet     planet_id;
            GswePlanetData *planet_data;
            GswePlanetInfo *planet_info;

            planet_id = GPOINTER_TO_INT(planet->data);
            planet_data = gswe_moment_get_planet(GSWE_MOMENT(priv->chart), planet_id, NULL);
            planet_info = gswe_planet_data_get_planet_info(planet_data);

            if ((current_widget = gtk_grid_get_child_at(GTK_GRID(priv->aspect_table), i + 1, i)) != NULL) {
                gtk_container_remove(GTK_CONTAINER(priv->aspect_table), current_widget);
            }

            label_hor = ag_window_create_planet_widget(planet_info);
            gtk_grid_attach(GTK_GRID(priv->aspect_table), label_hor, i + 1, i, 1, 1);

            if (i > 0) {
                if ((current_widget = gtk_grid_get_child_at(GTK_GRID(priv->aspect_table), 0, i)) != NULL) {
                    gtk_container_remove(GTK_CONTAINER(priv->aspect_table), current_widget);
                }

                label_ver = ag_window_create_planet_widget(planet_info);
                gtk_grid_attach(GTK_GRID(priv->aspect_table), label_ver, 0, i, 1, 1);
            }
        }

        priv->aspect_table_populated = TRUE;
    }

    for (planet1 = planet_list, i = 0; planet1; planet1 = g_list_next(planet1), i++) {
        for (planet2 = planet_list, j = 0; planet2; planet2 = g_list_next(planet2), j++) {
            GsweAspectData *aspect;
            GtkWidget      *aspect_widget;
            GError         *err = NULL;

            if (GPOINTER_TO_INT(planet1->data) == GPOINTER_TO_INT(planet2->data)) {
                break;
            }

            if ((aspect_widget = gtk_grid_get_child_at(GTK_GRID(priv->aspect_table), j + 1, i)) != NULL) {
                gtk_container_remove(GTK_CONTAINER(priv->aspect_table), aspect_widget);
            }

            if ((aspect = gswe_moment_get_aspect_by_planets(GSWE_MOMENT(priv->chart), GPOINTER_TO_INT(planet1->data), GPOINTER_TO_INT(planet2->data), &err)) != NULL) {
                GsweAspectInfo *aspect_info;

                aspect_info   = gswe_aspect_data_get_aspect_info(aspect);

                if (gswe_aspect_data_get_aspect(aspect) != GSWE_ASPECT_NONE) {
                    aspect_widget = ag_window_create_aspect_widget(aspect_info);
                    gtk_grid_attach(GTK_GRID(priv->aspect_table), aspect_widget, j + 1, i, 1, 1);
                }
            } else if (err) {
                g_warning("%s\n", err->message);
            } else {
                g_error("No aspect is returned between two planets. This is a bug in SWE-GLib!\n");
            }
        }
    }

    gtk_widget_show_all(priv->aspect_table);
}

void
ag_window_redraw_chart(AgWindow *window)
{
    GError          *err         = NULL;
    AgWindowPrivate *priv        = ag_window_get_instance_private(window);
    gchar           *svg_content = ag_chart_create_svg(priv->chart, NULL, &err);

    if (svg_content == NULL) {
        GtkWidget *dialog;

        dialog = gtk_message_dialog_new(GTK_WINDOW(window), 0, GTK_MESSAGE_WARNING, GTK_BUTTONS_OK, "Unable to draw chart: %s", err->message);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
    } else {
        webkit_web_view_load_string(
                WEBKIT_WEB_VIEW(priv->chart_web_view),
                svg_content, "image/svg+xml", "UTF-8", "file://");
        g_free(svg_content);
    }

    ag_window_redraw_aspect_table(window);
}

void
ag_window_update_from_chart(AgWindow *window)
{
    AgWindowPrivate *priv        = ag_window_get_instance_private(window);
    GsweTimestamp   *timestamp   = gswe_moment_get_timestamp(GSWE_MOMENT(priv->chart));
    GsweCoordinates *coordinates = gswe_moment_get_coordinates(GSWE_MOMENT(priv->chart));

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->year), gswe_timestamp_get_gregorian_year(timestamp, NULL));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->month), gswe_timestamp_get_gregorian_month(timestamp, NULL));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->day), gswe_timestamp_get_gregorian_day(timestamp, NULL));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->hour), gswe_timestamp_get_gregorian_hour(timestamp, NULL));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->minute), gswe_timestamp_get_gregorian_minute(timestamp, NULL));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->second), gswe_timestamp_get_gregorian_second(timestamp, NULL));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->timezone), gswe_timestamp_get_gregorian_timezone(timestamp));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->longitude), fabs(coordinates->longitude));

    if (coordinates->longitude < 0.0) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->west_long), TRUE);
    }

    gtk_spin_button_set_value(GTK_SPIN_BUTTON(priv->latitude), fabs(coordinates->latitude));

    if (coordinates->latitude < 0.0) {
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(priv->south_lat), TRUE);
    }

    gtk_entry_set_text(GTK_ENTRY(priv->name), ag_chart_get_name(priv->chart));

    g_free(coordinates);

    ag_window_redraw_chart(window);
}

static void
chart_changed(AgChart *chart, AgWindow *window)
{
    ag_window_redraw_chart(window);
}

static void
recalculate_chart(AgWindow *window)
{
    AgWindowPrivate *priv = ag_window_get_instance_private(window);
    gint            year      = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(priv->year)),
                    month     = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(priv->month)),
                    day       = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(priv->day)),
                    hour      = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(priv->hour)),
                    minute    = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(priv->minute)),
                    second    = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(priv->second));
    gdouble         longitude = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->longitude)),
                    latitude  = gtk_spin_button_get_value(GTK_SPIN_BUTTON(priv->latitude));
    gboolean        south     = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->south_lat)),
                    west      = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(priv->west_long));
    GsweTimestamp   *timestamp;

    g_debug("Recalculating chart data");

    if (south) {
        latitude = 0 - latitude;
    }

    if (west) {
        longitude = 0 - longitude;
    }

    // TODO: Set timezone according to the city selected!
    if (priv->chart == NULL) {
        timestamp = gswe_timestamp_new_from_gregorian_full(year, month, day, hour, minute, second, 0, 1.0);
        // TODO: make house system configurable
        priv->chart = ag_chart_new_full(timestamp, longitude, latitude, 380.0, GSWE_HOUSE_SYSTEM_PLACIDUS);
        g_signal_connect(priv->chart, "changed", G_CALLBACK(chart_changed), window);
        ag_window_redraw_chart(window);
    } else {
        timestamp = gswe_moment_get_timestamp(GSWE_MOMENT(priv->chart));
        gswe_timestamp_set_gregorian_full(timestamp, year, month, day, hour, minute, second, 0, 1.0, NULL);
    }

    ag_chart_set_name(priv->chart, gtk_entry_get_text(GTK_ENTRY(priv->name)));
}

void
ag_window_tab_changed_cb(GtkStack *stack, GParamSpec *pspec, AgWindow *window)
{
    GtkWidget       *active_tab;
    const gchar     *active_tab_name = gtk_stack_get_visible_child_name(stack);
    AgWindowPrivate *priv            = ag_window_get_instance_private(window);

    g_debug("Active tab changed: %s", active_tab_name);

    if (active_tab_name == NULL) {
        return;
    }

    active_tab = gtk_stack_get_visible_child(stack);

    if (strcmp("chart", active_tab_name) == 0) {
        gtk_widget_set_size_request(active_tab, 600, 600);
    }

    // If we are coming from the Edit tab, let’s assume the chart data has
    // changed. This is a bad idea, though, it should be checked instead!
    // (TODO)

    // Note that priv->current_tab is actually the previously selected tab, not
    // the real active one!
    if (priv->current_tab == priv->tab_edit) {
        recalculate_chart(window);
    }

    priv->current_tab = active_tab;
}

static void
ag_window_change_tab_action(GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
    AgWindow        *window     = AG_WINDOW(user_data);
    const gchar     *target_tab = g_variant_get_string(parameter, NULL);
    AgWindowPrivate *priv       = ag_window_get_instance_private(window);

    gtk_stack_set_visible_child_name(GTK_STACK(priv->stack), target_tab);
    g_action_change_state(G_ACTION(action), parameter);
}

static GActionEntry win_entries[] = {
    { "close",      ag_window_close_action,      NULL, NULL,      NULL },
    { "save",       ag_window_save_action,       NULL, NULL,      NULL },
    { "save-as",    ag_window_save_as_action,    NULL, NULL,      NULL },
    { "export-svg", ag_window_export_svg_action, NULL, NULL,      NULL },
    { "view-menu",  ag_window_view_menu_action,  NULL, "false",   NULL },
    { "gear-menu",  ag_window_gear_menu_action,  NULL, "false",   NULL },
    { "change-tab", ag_window_change_tab_action, "s",  "'edit'",  NULL },
};

static void
ag_window_display_changed(GSettings *settings, gchar *key, AgWindow *window)
{
    AgWindowPrivate *priv = ag_window_get_instance_private(window);

    /* The planet symbols are redrawn only if aspect_table_populated is
     * set to FALSE */
    if (g_str_equal("planets-char", key)) {
        priv->aspect_table_populated = FALSE;
    }

    ag_window_redraw_aspect_table(window);
}

static void
ag_window_init(AgWindow *window)
{
    GtkAccelGroup   *accel_group;
    GSettings       *main_settings;
    AgWindowPrivate *priv = ag_window_get_instance_private(window);

    gtk_widget_init_template(GTK_WIDGET(window));

    priv->settings = ag_settings_get();
    main_settings  = ag_settings_peek_main_settings(priv->settings);

    g_signal_connect(G_OBJECT(main_settings), "changed::planets-char", G_CALLBACK(ag_window_display_changed), window);
    g_signal_connect(G_OBJECT(main_settings), "changed::aspects-char", G_CALLBACK(ag_window_display_changed), window);

    webkit_web_view_load_string(
            WEBKIT_WEB_VIEW(priv->chart_web_view),
            "<html>" \
                "<head>" \
                    "<title>No Chart</title>" \
                "</head>" \
                "<body>" \
                    "<h1>No Chart</h1>" \
                    "<p>No chart is loaded. Create one on the edit view, or open one from the application menu!</p>" \
                "</body>" \
            "</html>",
            "text/html", "UTF-8", NULL);

    gtk_stack_set_visible_child_name(GTK_STACK(priv->stack), "edit");
    priv->current_tab = priv->tab_edit;
    g_object_set(priv->year_adjust, "lower", (gdouble)G_MININT, "upper", (gdouble)G_MAXINT, NULL);
    //TODO: gtk_header_bar_set_custom_title(GTK_HEADER_BAR(priv->header_bar), priv->stack_switcher);

    priv->chart    = NULL;
    priv->uri      = NULL;
    priv->settings = ag_settings_get();

    gtk_window_set_hide_titlebar_when_maximized(GTK_WINDOW(window), TRUE);

    g_action_map_add_action_entries(G_ACTION_MAP(window), win_entries, G_N_ELEMENTS(win_entries), window);

    accel_group = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window), accel_group);
}

static void
ag_window_dispose(GObject *gobject)
{
    AgWindowPrivate *priv = ag_window_get_instance_private(AG_WINDOW(gobject));

    g_clear_object(&priv->settings);

    G_OBJECT_CLASS(ag_window_parent_class)->dispose(gobject);
}

static void
ag_window_class_init(AgWindowClass *klass)
{
    GObjectClass   *gobject_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass *widget_class  = GTK_WIDGET_CLASS(klass);

    gobject_class->dispose = ag_window_dispose;

    gtk_widget_class_set_template_from_resource(widget_class, "/eu/polonkai/gergely/Astrognome/ui/ag-window.ui");
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, header_bar);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, name);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, year);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, month);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, day);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, hour);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, minute);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, second);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, timezone);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, north_lat);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, south_lat);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, east_long);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, west_long);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, latitude);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, longitude);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, chart_web_view);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, aspect_table);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, year_adjust);
    gtk_widget_class_bind_template_child_private(widget_class, AgWindow, stack);
}

gboolean
ag_window_chart_context_cb(WebKitWebView *web_view, GtkWidget *default_menu, WebKitHitTestResult *hit_test_result, gboolean triggered_with_keyboard, gpointer user_data)
{
    return TRUE;
}

static gboolean
ag_window_configure_event_cb(GtkWidget *widget, GdkEventConfigure *event, gpointer user_data)
{
    AgWindow        *window = AG_WINDOW(widget);
    AgWindowPrivate *priv   = ag_window_get_instance_private(window);

    ag_window_settings_save(GTK_WINDOW(window), ag_settings_peek_window_settings(priv->settings));

    return FALSE;
}

GtkWidget *
ag_window_new(AgApp *app)
{
    AgWindow        *window = g_object_new(AG_TYPE_WINDOW, NULL);
    AgWindowPrivate *priv   = ag_window_get_instance_private(window);

    gtk_window_set_application(GTK_WINDOW(window), GTK_APPLICATION(app));

    gtk_window_set_icon_name(GTK_WINDOW(window), "astrognome");
    g_signal_connect(window, "configure-event", G_CALLBACK(ag_window_configure_event_cb), NULL);

    ag_window_settings_restore(GTK_WINDOW(window), ag_settings_peek_window_settings(priv->settings));

    return GTK_WIDGET(window);
}

void
ag_window_set_chart(AgWindow *window, AgChart *chart)
{
    AgWindowPrivate *priv = ag_window_get_instance_private(window);

    if (priv->chart != NULL) {
        g_signal_handlers_disconnect_by_func(priv->chart, chart_changed, window);
        g_object_unref(priv->chart);
    }

    priv->chart = chart;
    g_signal_connect(priv->chart, "changed", G_CALLBACK(chart_changed), window);
    g_object_ref(chart);
}

AgChart *
ag_window_get_chart(AgWindow *window)
{
    AgWindowPrivate *priv = ag_window_get_instance_private(window);

    return priv->chart;
}

void
ag_window_set_uri(AgWindow *window, const gchar *uri)
{
    AgWindowPrivate *priv = ag_window_get_instance_private(window);

    if (priv->uri != NULL) {
        g_free(priv->uri);
    }

    priv->uri = g_strdup(uri);
}

gchar *
ag_window_get_uri(AgWindow *window)
{
    AgWindowPrivate *priv = ag_window_get_instance_private(window);

    return g_strdup(priv->uri);
}

void
ag_window_settings_restore(GtkWindow *window, GSettings *settings)
{
    gint      width,
              height;
    gboolean  maximized;
    GdkScreen *screen;

    width     = g_settings_get_int(settings, "width");
    height    = g_settings_get_int(settings, "height");
    maximized = g_settings_get_boolean(settings, "maximized");

    if ((width > 1) && (height > 1)) {
        gint max_width,
             max_height;

        screen     = gtk_widget_get_screen(GTK_WIDGET(window));
        max_width  = gdk_screen_get_width(screen);
        max_height = gdk_screen_get_height(screen);

        width  = CLAMP(width, 0, max_width);
        height = CLAMP(height, 0, max_height);

        gtk_window_set_default_size(window, width, height);
    }

    if (maximized) {
        gtk_window_maximize(window);
    }
}

void
ag_window_settings_save(GtkWindow *window, GSettings *settings)
{
    GdkWindowState state;
    gint           width,
                   height;
    gboolean       maximized;

    state     = gdk_window_get_state(gtk_widget_get_window(GTK_WIDGET(window)));
    maximized = ((state & GDK_WINDOW_STATE_MAXIMIZED) == GDK_WINDOW_STATE_MAXIMIZED);

    g_settings_set_boolean(settings, "maximized", maximized);

    gtk_window_get_size(window, &width, &height);
    g_settings_set_int(settings, "width", width);
    g_settings_set_int(settings, "height", height);
}

void
ag_window_change_tab(AgWindow *window, const gchar *tab_name)
{
    AgWindowPrivate *priv = ag_window_get_instance_private(window);

    gtk_stack_set_visible_child_name(GTK_STACK(priv->stack), tab_name);
    g_action_change_state(
            g_action_map_lookup_action(G_ACTION_MAP(window), "change-tab"),
            g_variant_new_string(tab_name)
        );
}

void
ag_window_name_changed_cb(GtkEntry *name_entry, AgWindow *window)
{
    const gchar     *name;
    AgWindowPrivate *priv = ag_window_get_instance_private(window);

    name = gtk_entry_get_text(name_entry);

    gtk_header_bar_set_subtitle(GTK_HEADER_BAR(priv->header_bar), name);
}
