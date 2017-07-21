#include <gst/gst.h>
#include <string.h>

typedef struct _MusicPlayer {
	GstElement *Mpipeline;
	GstElement *Msource;
	GstElement *Mconvert;
	GstElement *Msink;
	GstBus *Mbus;
	GMainLoop *Mloop;
	gboolean Malive;	/* Whether playing status. */
	gint64 Mduration;	/* Music duration time. */
	gdouble Mrate;		/* Playing speed. */
	gint64 Mcurrent;	/* Current position. */
	GstState Mstate;	/* Playing state. */
} MusicPlayer;

static void send_seek_event(MusicPlayer *Mdata) {
	GstEvent *seek_event;

	/* Create the seek event */
	if(Mdata->Mrate > 0) {
	seek_event = gst_event_new_seek(Mdata->Mrate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH |
		GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET, Mdata->Mcurrent, GST_SEEK_TYPE_NONE, 0);
	}
	else {
	seek_event = gst_event_new_seek(Mdata->Mrate, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH |
			 GST_SEEK_FLAG_ACCURATE, GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, Mdata->Mcurrent);
	}

	if(Mdata->Msink == NULL) {
		/* If we have not done so, obtain the sink through which we will send the seek events */
		g_object_get(Mdata->Mpipeline, "music-sink", &Mdata->Msink, NULL);
	}

	/* Send the event */
	gst_element_send_event(Mdata->Msink, seek_event);

	g_print("  Rate: %0.1f\n", Mdata->Mrate);

	return ;
}

static void pad_added_handler(GstElement *src, GstPad *new_pad, MusicPlayer *Mdata) {
	GstPadLinkReturn ret;
	GstCaps *new_pad_caps = NULL;
	GstStructure *new_pad_struct = NULL;
	const gchar *new_pad_type = NULL;
	guint caps_size;

	/* Get music-convert element sink pad. */
	GstPad *sink_pad = gst_element_get_static_pad(Mdata->Mconvert, "sink");
	if(gst_pad_is_linked(sink_pad)) {
		g_print("	We are already linked. Ignoring.\n");
		goto exit;
	}

	new_pad_caps = gst_pad_query_caps(new_pad, NULL);
	new_pad_struct = gst_caps_get_structure(new_pad_caps, 0);
	caps_size = gst_caps_get_size(new_pad_caps);
	new_pad_type = gst_structure_get_name(new_pad_struct);
	if(!g_str_has_prefix(new_pad_type, "audio/x-raw")) {
		g_print("	It has type '%s' which is not raw audio. Ignoring.\n", new_pad_type);
		goto exit;
	}

	/* Link "music-source" to "music-convert". */
	ret = gst_pad_link(new_pad, sink_pad);
	if(GST_PAD_LINK_FAILED(ret)) {
		g_printerr("Error: link pad '%s' to '%s' failed.\n",
			GST_PAD_NAME(new_pad), GST_PAD_NAME(sink_pad));
		g_main_loop_quit(Mdata->Mloop);
	}
	else {
		g_print("  Type: %s\n", new_pad_type);
		g_print("  Size: %d\n", caps_size);
		g_print("  Rate: 1.0\n");
	}

	if(!gst_element_query_duration(Mdata->Msink, GST_FORMAT_TIME, &Mdata->Mduration))
		g_printerr("Could'nt query duration.\n");
	else
		g_print("  Duration: %" GST_TIME_FORMAT "\n", GST_TIME_ARGS(Mdata->Mduration));

	return ;

exit:
	if(new_pad_caps != NULL)
		gst_caps_unref(new_pad_caps);
	gst_object_unref(sink_pad);
}

static void message_handler(GstBus *bus, GstMessage *msg, MusicPlayer *Mdata) {
	switch(GST_MESSAGE_TYPE(msg)) {
		case GST_MESSAGE_ERROR: {
			GError *err;
			gchar *debug;

			gst_message_parse_error(msg, &err, &debug);
			g_printerr("Error: %s\n", err->message);
			g_error_free(err);
			g_free(debug);

			gst_element_set_state(Mdata->Mpipeline, GST_STATE_READY);
			Mdata->Malive = FALSE;
			g_print("  Status: %s > READY\n", gst_element_state_get_name(Mdata->Mstate));
			g_main_loop_quit(Mdata->Mloop);
			break;
		}
		case GST_MESSAGE_EOS: {
			gst_element_set_state(Mdata->Mpipeline, GST_STATE_READY);
			Mdata->Malive = FALSE;
			g_print("  Status: %s > READY\n", gst_element_state_get_name(Mdata->Mstate));
			g_main_loop_quit(Mdata->Mloop);
			break;
		}
		case GST_MESSAGE_STATE_CHANGED: {
			if(GST_MESSAGE_SRC(msg) == GST_OBJECT(Mdata->Msink)) {
				GstState old_state, new_state, pending_state;

				gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
				Mdata->Malive = (new_state == GST_STATE_PLAYING);
				if(old_state == GST_STATE_PLAYING)
					g_print("\n");
				Mdata->Mstate = new_state;
				g_print("  Status: %s > %s\n", gst_element_state_get_name(old_state),
					gst_element_state_get_name(new_state));
			}
		}
	}

	return ;
}

static gboolean refresh_handler(MusicPlayer *Mdata) {
	static gboolean flag = FALSE;

	if(!Mdata->Malive)
		return TRUE;

	/* Get current position. */
	if(!gst_element_query_position(Mdata->Msource, GST_FORMAT_TIME, &Mdata->Mcurrent)) {
		g_printerr("Could'nt query current position.\n");
	}

	if(Mdata->Mcurrent < Mdata->Mduration) {
		g_print("  Position: %" GST_TIME_FORMAT "\r", GST_TIME_ARGS(Mdata->Mcurrent));
	}
	else {
		g_print("\n");
		return FALSE;
	}

	if(Mdata->Mcurrent > (Mdata->Mduration / 6) && Mdata->Mcurrent < (Mdata->Mduration/3)) {
		gst_element_seek_simple(Mdata->Msink, GST_FORMAT_TIME,
			GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT, Mdata->Mduration / 3);
		g_print("\n");
		flag = TRUE;
	}

	if(Mdata->Mcurrent > (Mdata->Mduration / 2) && flag) {
		g_print("\n");
		flag = FALSE;
		Mdata->Mrate = 2.0;
		send_seek_event(Mdata);
	}

	return TRUE;
}

int main(int argc, char *argv[]) {
	MusicPlayer Mdata;
	GstStateChangeReturn ret;

	if(argc < 2) {
		g_print("Usage: %s '(uri)resource'.\n", argv[0]);
		return -1;
	}

	/* Initialization. */
	gst_init(&argc, &argv);
	memset(&Mdata, 0, sizeof(Mdata));
	Mdata.Malive = FALSE;
	Mdata.Mrate = 1.0;
	Mdata.Mstate = GST_STATE_NULL;

	/* Create element and pipeline. */
	Mdata.Msource = gst_element_factory_make("uridecodebin", "music-source");
	Mdata.Mconvert = gst_element_factory_make("audioconvert", "music-convert");
	Mdata.Msink = gst_element_factory_make("autoaudiosink", "music-sink");
	Mdata.Mpipeline = gst_pipeline_new("music-pipeline");

	if(!Mdata.Mpipeline || !Mdata.Msource || !Mdata.Mconvert || !Mdata.Msink) {
		g_printerr("One or more elements could'nt be created.\n");
		return -1;
	}

	/* Add all element into bin. */
	gst_bin_add_many(GST_BIN(Mdata.Mpipeline), Mdata.Msource, Mdata.Mconvert, Mdata.Msink, NULL);
	/* Link 'music-convert' to 'music-sink'. */
	if(!gst_element_link(Mdata.Mconvert, Mdata.Msink)) {
		g_printerr("'music-convert' could'nt link with 'music-sink\n'");
		gst_object_unref(Mdata.Mpipeline);
		return -1;
	}

	/* Set the uri to play. */
	g_object_set(Mdata.Msource, "uri", argv[1], NULL);

	/* Handler event of "pad-added". */
	g_signal_connect(Mdata.Msource, "pad-added", G_CALLBACK(pad_added_handler), &Mdata);

	/* Set playing state. Start playing. */
	ret = gst_element_set_state(Mdata.Mpipeline, GST_STATE_PLAYING);
	if(ret == GST_STATE_CHANGE_FAILURE) {
		g_printerr("Unable to set pipeline to the playing state.\n");
		gst_object_unref(Mdata.Mpipeline);
		return -1;
	}

	/* Listen to the bus. */
	Mdata.Mbus = gst_element_get_bus(Mdata.Mpipeline);
	gst_bus_add_signal_watch(Mdata.Mbus);

	/* Handler 'message' signal. */
	g_signal_connect(Mdata.Mbus, "message", G_CALLBACK(message_handler), &Mdata);

	/* Main loop. */
	Mdata.Mloop = g_main_loop_new(NULL, FALSE);

	g_print("Runing...\n");
	g_print("\n");
	g_print("Information\n");

	g_timeout_add(1, (GSourceFunc)refresh_handler, &Mdata);
	g_main_loop_run(Mdata.Mloop);

	/* Free resources. */
	g_main_loop_unref(Mdata.Mloop);
	gst_element_set_state(Mdata.Mpipeline, GST_STATE_NULL);
	gst_object_unref(Mdata.Mbus);
	gst_object_unref(Mdata.Mpipeline);

	g_print("\n");
	g_print("Ending...\n");

	return 0;
}
