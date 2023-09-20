#!/bin/bash
rm -rf build_coverage
meson setup build_coverage -Db_coverage=true

# Unit test
ninja test -C build_coverage
# block test 
cd build_coverage
./nvidia-fdr &
sleep 120 && kill -SIGINT %1

# lcov
ninja coverage-html
# gcov - html
/usr/bin/gcovr -r /nvidia-fdr /nvidia-fdr/build_coverage -e /nvidia-fdr/subprojects -e /nvidia-fdr/tests -e /nvidia-fdr/build_coverage/nvidia-fdr.p/fdr_logs_schema.pb.cc -e /nvidia-fdr/build_coverage/nvidia-fdr.p/fdr_logs_schema.pb.h -o /nvidia-fdr/build_coverage/meson-logs/coverage.html --html-details --html-details-syntax-highlighting
# gcov - text
/usr/bin/gcovr -r /nvidia-fdr /nvidia-fdr/build_coverage -e /nvidia-fdr/subprojects  -e /nvidia-fdr/tests -e /nvidia-fdr/build_coverage/nvidia-fdr.p/fdr_logs_schema.pb.cc -e /nvidia-fdr/build_coverage/nvidia-fdr.p/fdr_logs_schema.pb.h -o /nvidia-fdr/build_coverage/meson-logs/coverage.txt
echo "Gcovr report:"
cat /nvidia-fdr/build_coverage/meson-logs/coverage.txt
echo -e "\nHtml format: ./build_coverage/meson-logs/coverage.html"