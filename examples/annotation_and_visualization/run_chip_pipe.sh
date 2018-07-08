#!/bin/sh

# Setup VIAME Paths (no need to run multiple times if you already ran it)

export VIAME_INSTALL=./../..

source ${VIAME_INSTALL}/setup_viame.sh 

# Run chip pipeline

pipeline_runner -p ${VIAME_INSTALL}/configs/pipelines/extract_chips_from_detections.pipe
