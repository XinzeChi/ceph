#!/bin/bash
# Min Chen <chenmin@xsky.com> 2015
# unittest for SN

declare -a ids;
created=0

source test/mon/mon-test-helpers.sh

function run() {
    local dir=$1

    export CEPH_MON="127.0.0.1:7501"
    export CEPH_ARGS
    CEPH_ARGS+="--fsid=$(uuidgen) --auth-supported=none "
    CEPH_ARGS+="--mon-host=$CEPH_MON "

    setup $dir || return 1
    run_mon $dir a --public-addr $CEPH_MON
    FUNCTIONS=${FUNCTIONS:-$(set | sed -n -e 's/^\(TEST_[0-9a-z_]*\) .*/\1/p')}
    for TEST_function in $FUNCTIONS ; do
        if ! $TEST_function $dir ; then
            cat $dir/a/log
            return 1
        fi
    done
    teardown $dir || return 1
}

function start_mon()
{
    local dir=$1
    shift
    local id=$1
    shift
    dir+=/$id

    ./ceph-mon \
        --id $id \
        --mon-osd-full-ratio=.99 \
        --mon-data-avail-crit=1 \
        --paxos-propose-interval=0.1 \
        --osd-crush-chooseleaf-type=0 \
        --osd-pool-default-erasure-code-directory=.libs \
        --debug-mon 20 \
        --debug-ms 20 \
        --debug-paxos 20 \
        --chdir= \
        --mon-data=$dir \
        --log-file=$dir/log \
        --mon-cluster-log-file=$dir/log \
        --run-dir=$dir \
        --pid-file=$dir/\$name.pid \
        "$@"
}

function create_osds()
{
    local N=$(($1))
    for ((i=0; i<$N; i++))
    do
        ids[$i]=`./ceph osd create`
    done
    if [ $N -gt 0 ];then
        created=1
    fi
}

function delete_osds()
{
    local N=$(($1));
    for ((i=N-1; i>=0; i--))
    do
      ./ceph osd rm $ids[$i];
    done
    if [ $N -gt 0 ];then
      created=0
    fi
}

function is_mon_alive()
{
    local mon_name=$1
    ps aux|grep -E "ceph-mon -i $mon_name"|grep -v grep
}

function check_mon_shutdown()
{
    local mon_name=$1
    logfile=testdir/ceph_sn/$mon_name/log
    grep -E "** Shutdown via OSDMonitor **" $logfile
}

function check_mon_cancel_shutdown()
{
    local mon_name=$1
    logfile=testdir/ceph_sn/$mon_name/log
    grep -E "cancel shoutdown this monitor" $logfile
}

function osd_count()
{
  ./ceph osd dump|grep -E "^osd"|wc -l
}

function TEST_sn_down()
{
    local func=TEST_sn_down

    SN="36472538-EAB0-E008-FC29-9A1E0937AFC3"
    ./ceph config-key put XSKY-SN $SN || return 1

    # osdmap osds > 10, trigger shutdown event
    if [ $created -ne 1 ];then
        create_osds 11
    fi

    sleep 320
    local down="`check_mon_shutdown a`"

    if [ "$down"x != ""x ];then
        return 0
    fi
    return 1
}

function TEST_sn_on()
{
    local func=TEST_sn_on
    if [ "`is_mon_alive a`"x = ""x ];then
        start_mon $dir a --public-addr $CEPH_MON
    fi

    osd_count || return 1
    local osds=$((`osd_count`))
    # TEST_sn_down leaves no less than 11 osds

    if [ $osds -lt 11 ];then
	create_osds $((11 - $osds))
    fi

    # osdmap osds > 10, trigger shutdown event
    sleep 200

    SN="9191D01F-C26B-D4E2-CE76-B9700C35D2D2"
    ./ceph config-key put XSKY-SN $SN || return 1
    sleep 120

    local down="`check_mon_cancel_shutdown a`"
    if [ "$down"x != ""x ];then
        return 0
    fi
    return 1
}

main ceph_sn
