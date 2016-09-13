#include <caffe/common.hpp>

#include "faster_rcnn_detector.h"

#include <opencv2/imgproc/imgproc.hpp>

#include <vital/types/vector.h>
#include <vital/io/eigen_io.h>
#include <vital/util/cpu_timer.h>
#include <vital/logger/logger.h>
#include <kwiversys/SystemTools.hxx>

#include <arrows/ocv/image_container.h>

#include <iostream>
#include <algorithm>
#include <cmath>
#include <math.h>


using caffe::Caffe;
using caffe::TEST;
using caffe::Net;
using caffe::Blob;

namespace kwiver {
namespace arrows {
namespace caffe {

class faster_rcnn_detector::priv
{
public:
  std::string m_prototxt_file;
  std::string m_classes_file;
  std::string m_caffe_model;
  vital::object_labels_sptr m_labels;
  double m_target_size;
  cv::Scalar m_pixel_means;
  double m_max_size;
  std::shared_ptr< Net< float > > m_net;
  bool m_use_gpu;
  int m_gpu_id;
  bool m_use_box_deltas;
  bool m_chip_image;
  unsigned int m_chip_width;
  unsigned int m_chip_height;
  unsigned int m_stride;

  kwiver::vital::logger_handle_t m_logger;

  std::pair<cv::Mat, double> prepair_image(cv::Mat const& in_image) const;
  std::vector< Blob<float>* > set_up_inputs(std::pair<cv::Mat, double> const& pair) const;

// =====================================================================
  priv()
  : m_labels(NULL),
    m_target_size(600),
    m_pixel_means(102.9801, 115.9465, 122.7717),
    m_max_size(1000),
    m_net(NULL),
    m_use_gpu(false),
    m_gpu_id(0),
    m_use_box_deltas(true),
    //TODO: redo these when parameterized
    m_chip_image(false),m_chip_width(450), m_chip_height(400), m_stride(375),
    m_logger( kwiver::vital::get_logger( "vital.algorithm" ) )
  {
  }


  priv(priv const& other)
  :m_prototxt_file(other.m_prototxt_file), m_classes_file(other.m_classes_file), m_caffe_model(other.m_caffe_model),
   m_labels(other.m_labels), m_target_size(other.m_target_size), m_pixel_means(other.m_pixel_means),
   m_max_size(other.m_max_size), m_net(other.m_net), m_use_gpu(other.m_use_gpu), m_gpu_id(other.m_gpu_id),
   m_use_box_deltas(other.m_use_box_deltas),
   m_chip_image(other.m_chip_image), m_chip_width(other.m_chip_width), m_chip_height(other.m_chip_height), m_stride(other.m_stride),
   m_logger( other.m_logger )
  {
  }


  ~priv()
  {
  }

};

faster_rcnn_detector::faster_rcnn_detector()
: d(new priv())
{
}

faster_rcnn_detector::faster_rcnn_detector(faster_rcnn_detector const& frd)
: d(new priv(*frd.d))
{
}

faster_rcnn_detector::~faster_rcnn_detector()
{
}

// --------------------------------------------------------------------
vital::config_block_sptr
faster_rcnn_detector::
get_configuration() const
{
  // Get base config from base class
  vital::config_block_sptr config = vital::algorithm::get_configuration();

   config->set_value("classes", d->m_classes_file,
                     "Text files containing the names of the classes supported by this faster rcnn");
   config->set_value("prototxt", d->m_prototxt_file,
                     "Points the the Prototxt file");
   config->set_value("caffe_model", d->m_caffe_model, "The file that contains the model");
   config->set_value("target_size", this->d->m_target_size, "TODO");
   config->set_value("max_size", this->d->m_max_size, "TODO");
   config->set_value("pixel_mean", vital::vector_3d(this->d->m_pixel_means[0], this->d->m_pixel_means[1], this->d->m_pixel_means[2]),
                     "The mean pixel value for the provided model");
   config->set_value("use_gpu", this->d->m_use_gpu, "use the gpu instead of the cpu");
   config->set_value("gpu_id", this->d->m_gpu_id, "what gpu to use");
   config->set_value("use_box_deltas", this->d->m_use_box_deltas, "use the learned jitter deltas");
   config->set_value( "chip_image",  this->d->m_chip_image, "TODO");
   config->set_value( "chip_width", this->d->m_chip_width, "TODO");
   config->set_value( "chip_height", this->d->m_chip_height, "TODO");
   config->set_value( "stride", this->d->m_stride, "TODO");

  return config;
}

namespace
{

inline std::string& rtrim(std::string& s)
{
    s.erase(s.find_last_not_of(" \t\n\r\f\v") + 1);
    return s;
}

inline std::string& ltrim(std::string& s)
{
    s.erase(0, s.find_first_not_of(" \t\n\r\f\v"));
    return s;
}

inline std::string& trim(std::string& s)
{
    return ltrim(rtrim(s));
}

} //end of anonymous namespace

// --------------------------------------------------------------------
void faster_rcnn_detector::
set_configuration(vital::config_block_sptr config_in)
{
  vital::config_block_sptr config = this->get_configuration();
  config->merge_config(config_in);

  this->d->m_classes_file  = config->get_value<std::string>("classes");
  this->d->m_prototxt_file = config->get_value<std::string>("prototxt");
  this->d->m_caffe_model = config->get_value<std::string>("caffe_model");
  this->d->m_use_gpu = config->get_value<bool>("use_gpu");
  this->d->m_gpu_id = config->get_value<bool>("gpu_id");
  this->d->m_use_box_deltas = config->get_value<bool>("use_box_deltas");
  this->d->m_chip_image = config->get_value<bool>( "chip_image" );
  this->d->m_chip_width = config->get_value<unsigned int>( "chip_width" );
  this->d->m_chip_height = config->get_value<unsigned int>( "chip_height" );
  this->d->m_stride = config->get_value<unsigned int>( "stride" );

  // Need to check for existance of files.

  if(d->m_use_gpu)
  {
    Caffe::SetDevice(this->d->m_gpu_id);
    Caffe::set_mode(Caffe::GPU);
  }
  else
  {
    Caffe::set_mode( Caffe::CPU );
  }
  this->d->m_net.reset( new Net< float >( this->d->m_prototxt_file, TEST) );
  this->d->m_net->CopyTrainedLayersFrom( this->d->m_caffe_model );

  std::vector<std::string> labels;
  std::ifstream in(this->d->m_classes_file.c_str());
  std::string line;
  while (std::getline(in, line))
  {
    if(line.empty()) continue;
    labels.push_back(trim(line));
  }

  this->d->m_labels = vital::object_labels_sptr(new vital::object_labels(labels));

  this->d->m_target_size = config->get_value<double>("target_size");

  vital::vector_3d tmp = config->get_value<vital::vector_3d>("pixel_mean");
  this->d->m_pixel_means = cv::Scalar(tmp.x(), tmp.y(), tmp.z());

  this->d->m_max_size = config->get_value<double>("max_size");
}


// --------------------------------------------------------------------
bool faster_rcnn_detector::
check_configuration(vital::config_block_sptr config) const
{
  if ( Caffe::mode() != ((d->m_use_gpu)?Caffe::GPU:Caffe::CPU))
  {
    if(d->m_use_gpu)
    {
      Caffe::SetDevice(this->d->m_gpu_id);
      Caffe::set_mode(Caffe::GPU);
    }
    else
    {
      Caffe::set_mode( Caffe::CPU );
    }
  }
  std::string classes = config->get_value<std::string>("classes");
  std::string prototxt = config->get_value<std::string>("prototxt");
  std::string caffemodel = config->get_value<std::string>("caffe_model");

  // check for any missing file name
  if ( classes.empty() || prototxt.empty() || caffemodel.empty() )
  {
     return false;
  }

  bool success( true );

  if ( ! kwiversys::SystemTools::FileExists( classes ) )
  {
     LOG_ERROR( d->m_logger, "classes file \"" << classes << "\" not found." );
     success = false;
  }

  if ( ! kwiversys::SystemTools::FileExists( prototxt ) )
  {
     LOG_ERROR( d->m_logger, "prototxt file \"" << prototxt << "\" not found." );
     success = false;
  }

  if ( ! kwiversys::SystemTools::FileExists( caffemodel ) )
  {
     LOG_ERROR( d->m_logger, "caffe_model file \"" << caffemodel << "\" not found." );
     success = false;
  }

  //config->print(std::cout);
  return success;
}


// --------------------------------------------------------------------
vital::detected_object_set_sptr
faster_rcnn_detector::
detect( vital::image_container_sptr image_data) const
{
  if(Caffe::mode() != ((d->m_use_gpu)?Caffe::GPU:Caffe::CPU))
  {
    if(d->m_use_gpu)
    {
      Caffe::SetDevice(this->d->m_gpu_id);
      Caffe::set_mode(Caffe::GPU);
    }
    else
    {
      Caffe::set_mode( Caffe::CPU );
    }
  }
  //Convert to opencv image
  if(image_data == NULL) return NULL;
  kwiver::vital::scoped_cpu_timer t( "Time to Detect Objects" );
  cv::Mat image = kwiver::arrows::ocv::image_container::vital_to_ocv(image_data->get_image());
  std::vector<cv::Mat> image_chips;
  std::vector<unsigned int> chip_x;
  std::vector<unsigned int> chip_y;
  if(this->d->m_chip_image)
  {
    for(unsigned int ux = 0; ux < image_data->width(); ux += this->d->m_stride)
    {
      unsigned int tux = ux;
      if(tux + this->d->m_chip_width > image_data->width())
      {
        tux = image_data->width()- this->d->m_chip_width -1;
        if(tux >= image_data->width()) continue;
      }
      for(unsigned int uy = 0; uy < image_data->height(); uy += this->d->m_stride)
      {
        unsigned int tuy = uy;
        if(tuy + this->d->m_chip_height > image_data->height())
        {
          tuy = image_data->height()-this->d->m_chip_height-1;
          if(tuy >= image_data->height()) continue;
        }
        cv::Mat cropedImage = image(cv::Rect(tux, tuy, this->d->m_chip_width, this->d->m_chip_height));
        image_chips.push_back(cropedImage);
        chip_x.push_back(tux);
        chip_y.push_back(tuy);
      }
    }
  }
  else
  {
    image_chips.push_back(image);
    chip_x.push_back(0);
    chip_y.push_back(0);
  }

  std::vector<vital::detected_object_sptr> detected_objects;
  for(size_t img_at = 0; img_at < image_chips.size(); ++img_at)
  {
    std::pair<cv::Mat, double> image_scale = this->d->prepair_image(image_chips[img_at]);
    std::vector< Blob<float>* > input_layers = this->d->set_up_inputs(image_scale);
    this->d->m_net->Forward(input_layers);

    //get output
    boost::shared_ptr< Blob< float > > rois = this->d->m_net->blob_by_name("rois");
    boost::shared_ptr< Blob< float > > probs = this->d->m_net->blob_by_name("cls_prob");
    boost::shared_ptr< Blob< float > > rois_deltas = this->d->m_net->blob_by_name("bbox_pred");

    const unsigned int roi_dim = rois->count() / rois->num();
    assert(roi_dim == 5);
    const unsigned int prob_dim = probs->count() / probs->num();
    assert(rois->num() == probs->num());

    for(int i = 0; i < rois->num(); ++i)
    {
      const float* start = rois->cpu_data() + rois->offset( i );
      double pts[4];
      //std::cout << start[0];
      for(unsigned int j = 1; j < roi_dim; ++j)
      {
        //std::cout << " " << start[j];///image_scale.second;
        pts[j-1] = start[j]/image_scale.second;
      }
      vital::detected_object::bounding_box bbox(vital::vector_2d(pts[0]+chip_x[img_at], pts[1]+chip_y[img_at]),
                                                vital::vector_2d(pts[2]+chip_x[img_at], pts[3]+chip_y[img_at]));

      //std::cout << "\t" << image_scale.second;

      start = probs->cpu_data() + probs->offset( i );
      //std::cout << " " << start[1] << std::endl;
      std::vector<double> tmpv;
      for(unsigned int j = 0; j < prob_dim; ++j)
      {
        tmpv.push_back(start[j]);
      }
      if(this->d->m_use_box_deltas && rois_deltas != NULL)
      {
        start = rois_deltas->cpu_data() + rois_deltas->offset( i );
        std::vector<double> tmpv2(tmpv.size(), vital::object_type::INVALID_SCORE);
        for( unsigned int j = 0; j < prob_dim; ++j )
        {
          unsigned int ds = j*4; // TODO calc for more rebustness
          float dx = start[ds];
          float dy = start[ds+1];
          float dw = start[ds+2];
          float dh = start[ds+3];
          tmpv2[j] = tmpv[j];
          vital::vector_2d center = bbox.center();
          float w = bbox.width();
          float h = bbox.height();
          center[0] = dx*w + center[0];
          center[1] = dy*h + center[1];
          float pw = exp(dw)*w;
          float ph = exp(dh)*h;
          vital::vector_2d halfS(pw*0.5, ph*0.5);
          vital::detected_object::bounding_box pbox(center-halfS, center+halfS);
          vital::object_type_sptr classification( new  vital::object_type(this->d->m_labels, tmpv2));
          detected_objects.push_back(vital::detected_object_sptr(new vital::detected_object(pbox, 1.0, classification)));
          tmpv2[j] = vital::object_type::INVALID_SCORE;
        }
      }
      else
      {
        vital::object_type_sptr classification( new  vital::object_type(this->d->m_labels, tmpv));
        detected_objects.push_back(vital::detected_object_sptr(new vital::detected_object(bbox, 1.0, classification)));
      }
    }
  }

  return vital::detected_object_set_sptr(new vital::detected_object_set(detected_objects, this->d->m_labels));
}

std::vector< Blob<float>* > faster_rcnn_detector::priv::set_up_inputs(std::pair<cv::Mat, double> const& pair) const
{
  cv::Size s = pair.first.size();
  int width = s.width;
  int height = s.height;

  std::vector< Blob<float>* > results;

  { //image layers
    std::vector<cv::Mat> input_channels;
    Blob<float>* image_layer = this->m_net->input_blobs()[0];
    image_layer->Reshape(1, //Number of images
                         pair.first.channels(), height, width);
    float* input_data = image_layer->mutable_cpu_data();
    for (int i = 0; i < image_layer->channels(); ++i)
    {
      cv::Mat channel(height, width, CV_32FC1, input_data);
      input_channels.push_back(channel);
      input_data += width * height;
    }
    cv::split(pair.first, input_channels);
    results.push_back(image_layer);
  }

  { //image data
    std::vector<int> input(2);
    input[0] = 1; //number of images
    input[1] = 3;
    Blob<float>* image_info = this->m_net->input_blobs()[1];
    image_info->Reshape(input);
    float* input_data = image_info->mutable_cpu_data();
    input_data[0] = height;
    input_data[1] = width;
    input_data[2] = pair.second;
    results.push_back(image_info);
  }

  return results;
}

// --------------------------------------------------------------------
std::pair<cv::Mat, double>
faster_rcnn_detector::priv::
prepair_image(cv::Mat const& in_image) const
{
  cv::Mat im_float;
  in_image.convertTo(im_float, CV_32F);
  im_float = im_float - this->m_pixel_means;

  double min_size = std::min(im_float.size[0], im_float.size[1]);
  double max_size = std::max(im_float.size[0], im_float.size[1]);

  double scale = this->m_target_size/min_size;
    //std::cout << scale << " " << scale * max_size << " " << MAX_SIZE << std::endl;
  if ( round(scale * max_size) > this->m_max_size)
  {
    scale = this->m_max_size / max_size;
  }
  cv::Mat scaleImage;
  cv::resize(im_float, scaleImage, cv::Size(), scale, scale);
  //std::cout << scale << std::endl;
  return std::pair<cv::Mat, double>(scaleImage, scale);
}

} } } //end namespace
