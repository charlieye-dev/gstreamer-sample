#include <gst/gst.h>
#include <string.h>

typedef struct _RecordCamera {
	GstElement *Rpipeline;
	GstElement *Raudiosource;
	GstElement *Rvideosource;
	GstElement *Raudioenc;
	GstElement *Rvideoenc;
	GstElement *Rmux;
	GstElement *Rqueue;
	GstElement *Rsink;
	GstBus *Rbus;
	GMainLoop *Rloop;
	GstState Rstate;
	gint64 Rcurrent;
} RecordCamera;

static gboolean refresh_handler(RecordCamera *Rdata) {
	if(!Rdata->Rstate == GST_STATE_PLAYING)
		return TRUE;

	if(!gst_element_query_position(Rdata->Rvideosource, GST_FORMAT_TIME, &Rdata->Rcurrent)) {
		g_printerr("Error: could not query current position.\n");
	}

	g_print("Duration: %" GST_TIME_FORMAT "\r", GST_TIME_ARGS(Rdata->Rcurrent));

	return TRUE;
}

int main(int argc, char *argv[]) {
	RecordCamera Rdata;
	GstStateChangeReturn ret;

	gst_init(&argc, &argv);
	memset(&Rdata, 0, sizeof(Rdata));
	Rdata.Rstate = GST_STATE_NULL;

	Rdata.Raudiosource = gst_element_factory_make("alsasrc", "audio-source");
	Rdata.Rvideosource = gst_element_factory_make("v4l2src", "video-source");
	Rdata.Raudioenc = gst_element_factory_make("voaacenc", "audio-enc");
	Rdata.Rvideoenc = gst_element_factory_make("jpegenc", "video-enc");
	Rdata.Rqueue = gst_element_factory_make("queue", "audio-queue");
	Rdata.Rmux = gst_element_factory_make("avimux", "record-mux");
	Rdata.Rsink = gst_element_factory_make("filesink", "record-sink");
	Rdata.Rpipeline = gst_pipeline_new("record-pipeline");

	if(!Rdata.Rpipeline || !Rdata.Raudiosource || !Rdata.Rvideosource || !Rdata.Rqueue ||
			!Rdata.Raudioenc || !Rdata.Rvideoenc || !Rdata.Rmux || !Rdata.Rsink) {
		g_printerr("Error: One or more element could't be created.\n");
		return -1;
	}

	gst_bin_add_many(GST_BIN(Rdata.Rpipeline), Rdata.Rvideosource, Rdata.Raudiosource,
			Rdata.Rqueue, Rdata.Raudioenc, Rdata.Rvideoenc, Rdata.Rmux, Rdata.Rsink, NULL);
	if(!gst_element_link_many(Rdata.Raudiosource, Rdata.Raudioenc, Rdata.Rqueue, Rdata.Rmux, NULL)) {
		g_printerr("Error(audio): Element could't be lined.\n");
		return -1;
	}
	if(!gst_element_link_many(Rdata.Rvideosource, Rdata.Rvideoenc, Rdata.Rmux, Rdata.Rsink, NULL)) {
		g_printerr("Error(video): Element could't be lined.\n");
		return -1;
	}

	g_object_set(Rdata.Rsink, "location", argv[1], NULL);
	g_object_set(Rdata.Raudiosource, "device", "hw:1,0", NULL);
	g_object_set(Rdata.Rvideosource, "device", "/dev/video0", NULL);

	ret = gst_element_set_state(Rdata.Rpipeline, GST_STATE_PLAYING);

	Rdata.Rloop	= g_main_loop_new(NULL, FALSE);

	g_print("Recording...\n");
	g_print("\n");

	g_timeout_add(1, (GSourceFunc)refresh_handler, &Rdata);
	g_main_loop_run(Rdata.Rloop);

	g_main_loop_unref(Rdata.Rloop);
	gst_element_set_state(Rdata.Rpipeline, GST_STATE_NULL);
	gst_object_unref(Rdata.Rpipeline);

	return 0;
}
