/*ckwg +29
 * Copyright 2017-2018 by Kitware, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither name of Kitware, Inc. nor the names of any contributors may be used
 *    to endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 * \brief Implementation of detected object set csv output
 */

#include "write_object_track_set_viame_csv.h"

#include <ctime>

namespace viame {


// -------------------------------------------------------------------------------
class write_object_track_set_viame_csv::priv
{
public:
  priv( write_object_track_set_viame_csv* parent)
    : m_parent( parent )
    , m_logger( kwiver::vital::get_logger( "write_object_track_set_viame_csv" ) )
    , m_first( true )
    , m_frame_number( 0 )
    , m_delim( "," )
    , m_stream_identifier( "" )
    , m_active_writing( false )
  { }

  ~priv() { }

  write_object_track_set_viame_csv* m_parent;
  kwiver::vital::logger_handle_t m_logger;
  bool m_first;
  int m_frame_number;
  std::string m_delim;
  std::string m_stream_identifier;
  std::map< unsigned, kwiver::vital::track_sptr > m_tracks;
  bool m_active_writing;
};


// ===============================================================================
write_object_track_set_viame_csv
::write_object_track_set_viame_csv()
  : d( new write_object_track_set_viame_csv::priv( this ) )
{
}


write_object_track_set_viame_csv
::~write_object_track_set_viame_csv()
{
}


void write_object_track_set_viame_csv
::close()
{
  if( d->m_active_writing )
  {
    // No flushing required
    write_object_track_set::close();
    return;
  }

  for( auto trk_pair : d->m_tracks )
  {
    auto trk_ptr = trk_pair.second;

    for( auto ts_ptr : *trk_ptr )
    {
      kwiver::vital::object_track_state* ts =
        dynamic_cast< kwiver::vital::object_track_state* >( ts_ptr.get() );

      if( !ts )
      {
        LOG_ERROR( d->m_logger, "Invalid timestamp " << trk_ptr->id()
                                                     << " " << trk_ptr->size() );
        continue;
      }

      kwiver::vital::detected_object_sptr det = ts->detection;
      const kwiver::vital::bounding_box_d empty_box =
        kwiver::vital::bounding_box_d( -1, -1, -1, -1 );
      kwiver::vital::bounding_box_d bbox = ( det ? det->bounding_box() : empty_box );
      auto confidence = ( det ? det->confidence() : 0 );
  
      stream() << trk_ptr->id() << d->m_delim           // 1: track id
               << d->m_stream_identifier << d->m_delim  // 2: video or image id
               << ts->frame() << d->m_delim             // 3: frame number
               << bbox.min_x() << d->m_delim            // 4: TL-x
               << bbox.min_y() << d->m_delim            // 5: TL-y
               << bbox.max_x() << d->m_delim            // 6: BR-x
               << bbox.max_y() << d->m_delim            // 7: BR-y
               << confidence << d->m_delim              // 8: confidence
               << "0";                                  // 9: length
  
      if( det )
      {
        const auto dot = det->type();
        if( dot )
        {
          const auto name_list( dot->class_names() );
          for( auto name : name_list )
          {
            stream() << d->m_delim << name << d->m_delim << dot->score( name );
          }
        }
        stream() << std::endl;
      }
    }
  }

  write_object_track_set::close();
}


// -------------------------------------------------------------------------------
void
write_object_track_set_viame_csv
::set_configuration( kwiver::vital::config_block_sptr config )
{
  d->m_delim =
    config->get_value<std::string>( "delimiter", d->m_delim );
  d->m_stream_identifier =
    config->get_value<std::string>( "stream_identifier", d->m_stream_identifier );
  d->m_active_writing =
    config->get_value<bool>( "active_writing", d->m_active_writing );
}


// -------------------------------------------------------------------------------
bool
write_object_track_set_viame_csv
::check_configuration( kwiver::vital::config_block_sptr config ) const
{
  return true;
}


// -------------------------------------------------------------------------------
void
write_object_track_set_viame_csv
::write_set( const kwiver::vital::timestamp& ts,
             const kwiver::vital::object_track_set_sptr set )
{
  if( d->m_first )
  {
    std::time_t rawtime;
    struct tm * timeinfo;

    time ( &rawtime );
    timeinfo = localtime ( &rawtime );
    char* cp =  asctime( timeinfo );
    cp[ strlen( cp )-1 ] = 0; // remove trailing newline
    const std::string atime( cp );

    // Write file header(s)
    stream() << "# 1: Detection or Track-id,"
             << "  2: Video or Image Identifier,"
             << "  3: Unique Frame Identifier,"
             << "  4-7: Img-bbox(TL_x,TL_y,BR_x,BR_y),"
             << "  8: Detection confidence,"
             << "  9: Fish Length (0 or -1 if invalid),"
             << "  10-11+: Repeated Species, Confidence Pairs"
             << std::endl;

    stream() << "# Written on: " << atime
             << "   by: write_object_track_set_viame_csv"
             << std::endl;

    d->m_first = false;
  }

  if( !d->m_active_writing )
  {
    for( auto trk : set->tracks() )
    {
      d->m_tracks[ trk->id() ] = trk;
    }
  }
  else
  {
    for( auto trk_ptr : set->tracks() )
    {
      if( !trk_ptr || trk_ptr->empty() )
      {
        LOG_ERROR( d->m_logger, "Received invalid track" );
        continue;
      }

      kwiver::vital::object_track_state* state =
        dynamic_cast< kwiver::vital::object_track_state* >( trk_ptr->back().get() );

      if( !state )
      {
        LOG_ERROR( d->m_logger, "Invalid track state for track "
                                << trk_ptr->id()
                                << " of length "
                                << trk_ptr->size() );
        continue;
      }

      if( state->frame() != ts.get_frame() )
      {
        // Last state is in the past, it was already written.
        continue;
      }

      kwiver::vital::detected_object_sptr det = state->detection;
      const kwiver::vital::bounding_box_d empty_box =
        kwiver::vital::bounding_box_d( -1, -1, -1, -1 );
      kwiver::vital::bounding_box_d bbox = ( det ? det->bounding_box() : empty_box );
      auto confidence = ( det ? det->confidence() : 0 );
  
      stream() << trk_ptr->id() << d->m_delim           // 1: track id
               << d->m_stream_identifier << d->m_delim  // 2: video or image id
               << state->frame() << d->m_delim          // 3: frame number
               << bbox.min_x() << d->m_delim            // 4: TL-x
               << bbox.min_y() << d->m_delim            // 5: TL-y
               << bbox.max_x() << d->m_delim            // 6: BR-x
               << bbox.max_y() << d->m_delim            // 7: BR-y
               << confidence << d->m_delim              // 8: confidence
               << "0";                                  // 9: length
  
      if( det )
      {
        const auto dot = det->type();
        if( dot )
        {
          const auto name_list( dot->class_names() );
          for( auto name : name_list )
          {
            stream() << d->m_delim << name << d->m_delim << dot->score( name );
          }
        }
        stream() << std::endl;
      }
    }
  }
}

} // end namespace
