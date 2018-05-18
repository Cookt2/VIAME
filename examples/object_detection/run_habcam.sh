#!/bin/sh

# Setup VIAME Paths (no need to run multiple times if you already ran it)

export VIAME_INSTALL=./../..

source ${VIAME_INSTALL}/setup_viame.sh 

# Run pipeline

pipeline_runner -p ${VIAME_INSTALL}/configs/pipelines/detector_habcam_system.pipe \
                -s input:image_list_file=input_image_list_habcam.txt
