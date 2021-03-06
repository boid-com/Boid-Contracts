#!/usr/bin/env bash

source `dirname $BASH_SOURCE`/common.sh

COMMON_PATH=../_compiled_contracts/$CONTRACT

source $COMMON_PATH/jungle/compile.sh
source $COMMON_PATH/mainnet_votingdisabled/compile.sh
source $COMMON_PATH/mainnet_votingenabled/compile.sh
source $COMMON_PATH/unit_tests/compile.sh
