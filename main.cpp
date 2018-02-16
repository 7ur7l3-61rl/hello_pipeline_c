#include <iostream>
#include <future>
#include <fstream>

#include <string.h>

#include <glib.h>
#include <gst/gst.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>


using namespace std;

const size_t read_block_size = (32*1024);

static size_t getFileSize(const std::string &fileName)
{
  ifstream file(fileName.c_str(), ifstream::in | ifstream::binary);
  size_t file_size = 0;

  if(file.is_open()) {
    file.seekg(0, ios::end);
    file_size = file.tellg();
    file.close();
  }

  return file_size;
}

class PipelineContainer
{
public:
  PipelineContainer(std::string file_name);
  bool Initialize();
  void SetUpMessageHandler();
  void printCaps(GstPad * pad);
  bool hasPadName(GstPad * pad, string candidate);
  bool isVideo(GstPad * pad);
  bool isAudio(GstPad * pad);
  void ListenForDataNeeded();
  void ListenForEnoughData();
  void ListenForSeekData();
  void ListenForPads();
  void StartPipeline();
  void StopPipeline();

  GstElement * pipeline = nullptr;
  GMainLoop * main_loop = nullptr;
  GstBus * bus = nullptr;

  GstElement * source = nullptr;
  GstElement * decodebin = nullptr;
  GstElement * audio_sink = nullptr;
  GstElement * video_sink = nullptr;

  size_t remaining_size = 0;
  std::string fileName;
  ifstream pipeline_input;
};

static void
dataNeeded (GstElement * pipeline, guint bytes_needed, PipelineContainer * container)
{
  cout << __FUNCTION__ << " : bytes needed : " << bytes_needed << endl;

  size_t buf_size = min(container->remaining_size, size_t(bytes_needed));
  char src_buf[buf_size];

  cout << __FUNCTION__ << " : read " << buf_size << " remaining size " << container->remaining_size << endl;
  container->pipeline_input.read(src_buf, buf_size);

  cout << __FUNCTION__ << " : create buffer " << buf_size << endl;
  GstBuffer *buf = gst_buffer_new_allocate (NULL, buf_size, NULL);

  cout << __FUNCTION__ << " : copy into buffer num bytes " << buf_size << endl;
  GstMapInfo map;
  gst_buffer_map (buf, &map, GST_MAP_WRITE);
  memcpy( (guchar *)map.data, src_buf, buf_size);

  cout << __FUNCTION__ << " : push buffer with bytes " << buf_size << endl;
  GstFlowReturn ret;
  g_signal_emit_by_name (container->source, "push-buffer", buf, &ret);

  gst_buffer_unref(buf);

  if (ret == GST_FLOW_OK)
    cout << __FUNCTION__ << " : Push Buffer successful" << endl;
  else
    cout << __FUNCTION__ << " : Push Buffer failed" << endl;

  container->remaining_size -= buf_size;
  cout << __FUNCTION__ << " : remaining size " << container->remaining_size << endl;
}

static void
enoughData (GstElement * pipeline, PipelineContainer * container)
{
  cout << __FUNCTION__ << endl;
}

static gboolean
seekData (GstElement * appsrc, guint64 position, PipelineContainer * container)
{
  cout << __FUNCTION__ << " : seek to position " << position << endl;
  return TRUE;
}

static void
seek_to_time (GstElement *pipeline,
              gint64      time_nanoseconds)
{
  if (!gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
                         GST_SEEK_TYPE_SET, time_nanoseconds,
                         GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {

    cout << __FUNCTION__ << " : Seek failed" << endl;
  }
}

static gboolean
handleMessage (GstBus * bus, GstMessage * message, PipelineContainer * container) {
  string message_name = GST_OBJECT_NAME (message->src);
  cout << __FUNCTION__ << " : Message received: " << message_name << endl;
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_EOS:

      cout << __FUNCTION__ << " : Got " << message_name << ", seek to the beginning";
      seek_to_time(container->pipeline, 0);

      cout << __FUNCTION__ << " : Got " << message_name << ", stopping message loop" << endl;
      g_main_loop_quit (container->main_loop);
      break;
    case GST_MESSAGE_ERROR:
      cout << __FUNCTION__ << " : Got " << message_name << ", stopping message loop" << endl;
      g_main_loop_quit (container->main_loop);
      break;
    default:
      break;
  }
  return true;
}

static void
padAdded (GstElement * element, GstPad * pad, PipelineContainer * container)
{
  gchar * pad_name = gst_pad_get_name (pad);
  gchar * element_name = gst_element_get_name (element);

  cout << __FUNCTION__ << " : Pad added to : " << element_name << endl;
  cout << __FUNCTION__ << " : Pad added    : " << pad_name << endl;

  container->printCaps(pad);

  if (container->isVideo(pad)) {
    cout << __FUNCTION__ << " : Trying to link video pad : " << pad_name << endl;
    gst_element_link_pads(element, pad_name, container->video_sink, "sink");
    cout << __FUNCTION__ << " : Pads linked " << endl;
  } else if (container->isAudio(pad)) {
    cout << __FUNCTION__ << " : Trying to link audio pad : " << pad_name << endl;
    gst_element_link_pads(element, pad_name, container->audio_sink, "sink");
    cout << __FUNCTION__ << " : Pads linked " << endl;
  } else {
    cout << __FUNCTION__ << " : Unknown pad type : " << pad_name << endl;
    cout << __FUNCTION__ << " : Pads could not be linked" << endl;
  }

  g_free (pad_name);
  g_free (element_name);
}

PipelineContainer::PipelineContainer(std::string file_name) :
  fileName(file_name),
  pipeline_input(file_name, ios::in|ios::binary) {
  cout << __FUNCTION__ << ": Create Mainloop" << endl;
  main_loop = g_main_loop_new (NULL, TRUE);
}

bool PipelineContainer::Initialize()
{
  cout << __FUNCTION__ << ": Check file" << endl;
  if (!pipeline_input.is_open()) {
    cout << __FUNCTION__ << ": Could not open input file " << endl;
    return false;
  }

  remaining_size = getFileSize(fileName);

  cout << __FUNCTION__ << ": Check file size " << remaining_size << endl;
  if (remaining_size <= 0) {
    cout << __FUNCTION__ << ": Invalid input file size " << remaining_size << endl;
    return false;
  }

  cout << __FUNCTION__ << ": Create Pipeline" << endl;
  pipeline = gst_pipeline_new ("test-pipeline");

  cout << __FUNCTION__ << ": Create Elements" << endl;

  source = gst_element_factory_make ("appsrc", "source");
  decodebin = gst_element_factory_make ("decodebin", "decoder");
  video_sink = gst_element_factory_make("appsink", "video_sink");
  audio_sink = gst_element_factory_make("appsink", "audio_sink");

  cout << __FUNCTION__ << ": Add the Elements to the Pipeline" << endl;
  gst_bin_add_many (GST_BIN (pipeline), source, decodebin, video_sink, audio_sink, NULL);

  cout << __FUNCTION__ << ": Link the source Element to the Decoder" << endl;
  gst_element_link_many (source, decodebin, NULL);

  bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));

  return true;
}

void PipelineContainer::SetUpMessageHandler() {
  cout << __FUNCTION__ << ":  Add callback function for error or eos on bus" << endl;
  gst_bus_add_watch (bus, (GstBusFunc) handleMessage, this);
}

void PipelineContainer::printCaps(GstPad * pad) {
  const GstCaps * caps = gst_pad_get_current_caps (pad);
  if (caps)
    cout << __FUNCTION__ << " Caps for pad " << gst_caps_to_string(caps) << endl;
  else
    cout << __FUNCTION__ << " Pad has no caps" << endl;
}

bool PipelineContainer::hasPadName(GstPad * pad, string candidate) {
  const GstCaps * caps = gst_pad_get_current_caps (pad);
  if (!caps)
    return false;

  GstStructure * structure = gst_caps_get_structure(caps, 0);
  if (!structure)
    return false;

  string structure_name = gst_structure_get_name(structure);

  cout << __FUNCTION__ << " : candidate " << candidate << " actual " << structure_name;

  return candidate == structure_name;
}


bool PipelineContainer::isVideo(GstPad * pad) {
  return hasPadName(pad, "video/x-raw");
}

bool PipelineContainer::isAudio(GstPad * pad) {
  return hasPadName(pad, "audio/x-raw");
}

void PipelineContainer::ListenForDataNeeded() {
  cout << __FUNCTION__ << ":  Add callback function need data on appsrc" << endl;
  cout << __FUNCTION__ << ": file size " << remaining_size << endl;
  g_signal_connect (source, "need-data", G_CALLBACK (dataNeeded), this);
}

void PipelineContainer::ListenForEnoughData() {
  cout << __FUNCTION__ << ":  Add callback function enough data on appsrc" << endl;
  g_signal_connect (source, "enough-data", G_CALLBACK (enoughData), this);
}

void PipelineContainer::ListenForSeekData() {
  cout << __FUNCTION__ << ":  Add callback function seek data on appsrc" << endl;
  g_signal_connect (source, "seek-data", G_CALLBACK (seekData), this);
}

void PipelineContainer::ListenForPads() {
  cout << __FUNCTION__ << ":  Add callback function for new pad on decodebin" << endl;

  g_signal_connect (decodebin, "pad-added", G_CALLBACK (padAdded), this);
}

void PipelineContainer::StartPipeline() {
  cout << __FUNCTION__ << ": Set Pipeline State to Playing " << endl;
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
}

void PipelineContainer::StopPipeline() {
  cout << __FUNCTION__ << ": Set Pipeline State to Null " << endl;
  gst_element_set_state (pipeline, GST_STATE_NULL);
}


int main(int argc, char *argv[])
{
  if (argc != 2) {
    cout << __FUNCTION__ << ": You need to pass in the input filename" << endl;
    return EXIT_FAILURE;
  }

  std::string fileName = argv[1];
  cout << __FUNCTION__ << ": Input file " << fileName << endl;

  cout << __FUNCTION__ << ": Initialize GStreamer" << endl;
  gst_init (&argc, &argv);


  cout << __FUNCTION__ << ": Create PipelineContainer" << endl;
  PipelineContainer pipeline_container(fileName);

  cout << __FUNCTION__ << ": Initialize PipelineContainer" << endl;
  bool successful = pipeline_container.Initialize();

  if (!successful) {
    cout << __FUNCTION__ << ": Pipeline initialization failed" << endl;
    return EXIT_FAILURE;
  }

  cout << __FUNCTION__ << ": Setup Message Handling" << endl;
  pipeline_container.SetUpMessageHandler();

  cout << __FUNCTION__ << ": Setup Listener for Pads on Decodebin" << endl;
  pipeline_container.ListenForPads();

  cout << __FUNCTION__ << ": Setup Listener for Data Needed on AppSrc" << endl;
  pipeline_container.ListenForDataNeeded();

  cout << __FUNCTION__ << ": Setup Listener for Enough Data on AppSrc" << endl;
  pipeline_container.ListenForEnoughData();

  cout << __FUNCTION__ << ": Setup Listener for Seek Data on AppSrc" << endl;
  pipeline_container.ListenForSeekData();

  cout << __FUNCTION__ << ": Start Pipeline" << endl;
  pipeline_container.StartPipeline();

  auto video_sink_reader_handle = async(launch::async, [&] () {
    cout << " Video Sink Reader started " << endl;

    GstAppSink *appsink = GST_APP_SINK(pipeline_container.video_sink);
    size_t num_samples = 0;

    while (true) {
      GstSample * sample = gst_app_sink_pull_sample(appsink);

      if (sample) {
        num_samples++;
        cout << " Video Sink Reader : Got sample number : " << num_samples << endl;

        GstCaps * caps = gst_sample_get_caps(sample);
        GstBuffer * buffer = gst_sample_get_buffer(sample);

        static GstStructure *s;
        const GstStructure *info = gst_sample_get_info(sample);

        if(!caps) {
          cout << " Video Sink Reader : Sample has no caps" << endl;
          break;
        }

        GstStructure * structure = gst_caps_get_structure(caps , 0);

        gint width = 0;
        gint height = 0;
        gst_structure_get_int(structure, "width", &width);
        gst_structure_get_int(structure, "height", &height);

        cout << " Video Sink Reader : width " << width << " height " << height << endl;

        gst_sample_unref (sample);

      } else {
        cout << " Video Sink Reader : No more samples, total number of samples " << num_samples << endl;
        break;
      }
    }
  });

  auto audio_sink_reader_handle = async(launch::async, [&] () {
    cout << " Audio Sink Reader started " << endl;

    GstAppSink *appsink = GST_APP_SINK(pipeline_container.audio_sink);
    size_t num_samples = 0;

    while (true) {
      GstSample * sample = gst_app_sink_pull_sample(appsink);

      if (sample) {
        num_samples++;
        cout << " Audio Sink Reader : Got sample number : " << num_samples << endl;

        GstCaps * caps = gst_sample_get_caps(sample);
        GstBuffer * buffer = gst_sample_get_buffer(sample);

        static GstStructure *s;
        const GstStructure *info = gst_sample_get_info(sample);

        if(!caps) {
          cout << " Audio Sink Reader : Sample has no caps" << endl;
          break;
        }

        GstStructure * structure = gst_caps_get_structure(caps , 0);

        gint rate = 0;
        gint channels = 0;
        gst_structure_get_int(structure, "rate", &rate);
        gst_structure_get_int(structure, "channels", &channels);

        cout << " Audio Sink Reader : rate " << rate << " channels " << channels << endl;

        gst_sample_unref (sample);

      } else {
        cout << " Audio Sink Reader : No more samples, total number of samples " << num_samples << endl;
        break;
      }
    }
  });

  cout << __FUNCTION__ << ": Start Message Loop" << endl;

  g_main_loop_run (pipeline_container.main_loop);

  video_sink_reader_handle.get();
  audio_sink_reader_handle.get();

  cout << __FUNCTION__ << ": Stop Pipeline" << endl;
  pipeline_container.StopPipeline();

  gst_object_unref (pipeline_container.bus);
  g_main_loop_unref (pipeline_container.main_loop);

  return EXIT_SUCCESS;
}
