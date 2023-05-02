#!/usr/bin/env bash

# arg variable init
output_dir=
run_stress=
args=()
# by default run one job per cpu thread
nr_cpus=$(grep -w processor /proc/cpuinfo|wc -l)
while [[ $# -gt 0 ]]; do
	case $1 in
	-o|--output-dir)
		# this outputs the logs for each test to a separate file
		output_dir="$2"
		shift
		shift
		;;
	-s|--run-stress-tests)
		# stress tests take a long time to run and are disabled by default
		run_stress=1
		shift
		;;
	-j|--jobs)
		# override the default job count
		nr_cpus=$2
		shift
		shift
		;;
	-h|--help)
		echo "./test-runner.sh [-o|--output-dir logfile_dir] [-s|--run-stress-tests] [-j|--jobs N] path/to/tests/d3d12"
		exit 1
		;;
	-*|--*)
		echo "unknown arg $1"
		;;
	*)
		args+=("$1")
		shift
		;;
	esac
done

set -- "${args[@]}"
d3d12_bin="$1"

if [[ -z $d3d12_bin || ! -f "$d3d12_bin" ]] ; then
	echo "Must specify path to valid d3d12 test exe!"
	exit 1
fi

tests=()
if [[ -z $run_stress ]] ; then
	tests=($(grep -w decl_test tests/d3d12_tests.h|grep -v stress|cut -d'(' -f2|cut -d')' -f1))
else
	tests=($(grep -w decl_test tests/d3d12_tests.h|cut -d'(' -f2|cut -d')' -f1))
fi

# runtime variable init
nr_tests=${#tests[@]}
if [ $nr_tests -lt 1 ] ; then
	echo "No tests detected! Is $d3d12_bin a valid test exe?"
	exit 1
fi

pids=()
# the counter for the number of tests running
counter=0
# the index for the next test to run
test_idx=0

if [[ -n $output_dir ]] ; then
	mkdir -p "$output_dir"
fi

# start test processes until $nr_cpus test processes are running
run_tests() {
	while (($counter < $nr_cpus)) ; do
		# output to /dev/null by default
		if [[ -z "$output_dir" ]] ; then
			VKD3D_TEST_FILTER=${tests[$test_idx]} "$d3d12_bin" &>/dev/null &
		else
			VKD3D_TEST_FILTER=${tests[$test_idx]} "$d3d12_bin" &> "$output_dir/${tests[$test_idx]}.log" &
		fi
		# capture pid of subprocess
		pids[$test_idx]=${!}
		#increment running test counter and test index
		counter=$((counter+1))
		test_idx=$((test_idx+1))
	done
}


echo "Running $nr_tests vkd3d-proton D3D12 tests..."
r=0
fails=()
last_notify=$(date +%s)
while (($r<$nr_tests)) ; do
	# run tests if there are more tests to run
	if [ $test_idx -lt $nr_tests ] ; then
		run_tests
	fi
	# tests are active: wait for one to finish, store pid to $finished
	wait -n -p finished &>/dev/null
	retval=$?
	pid_count=${#pids[@]}
	# iterate $pids array to find the index matching $finished
	for ((c=0; c < $pid_count; c++)) ; do
		if [[ ${pids[$c]} == $finished ]] ; then
			# failed/crashed tests have $retval!=0
			if [[ $retval != 0 ]] ; then
				# print failure immediately and store for summary
				fails+=(${tests[$c]})
				echo "FAILED ${tests[$c]}"
			fi
			# increment the "done" counter
			r=$((r+1))
			# decrement the "running" counter
			counter=$((counter-1))
			break
		fi
	done
	cur_time=$(date +%s)
	# only notify every 5s
	if [ $((last_notify+5)) -lt $cur_time ] ; then
		echo "$r / $nr_tests complete (${#fails[@]} failures)..."
		last_notify=$cur_time
	fi
done


# nice summary output at the end
echo "***********************"
echo "Finished in ${SECONDS}s!"

if [[ "${#fails[@]}" != 0 ]] ; then
	echo "${#fails[@]} FAILURES: (run with 'VKD3D_TEST_FILTER=<name> $d3d12_bin')"
	for fail in "${fails[@]}" ; do
		echo "$fail"
	done
else
	echo "ALL PASSED!"
fi
