cjson_opt: cJSON.c cJSON_Flatten_Opt.c
	gcc -O3 $^ -lm -o $@
	strip $@

cjson_env_sh: cjson_opt
	ln -f $^ $@

.PHONY: test_getenv_wrap
test_getenv_wrap: getenv_wrap.c getenv_test.c | cjson_opt
	gcc $^ -Wl,-wrap,getenv -o $@
	# ./cjson_opt --set-env-exec test.json ./$@
	./cjson_opt --set-env-shell test.json ./$@

.PHONY: test_getenv_preload
test_getenv_preload: getenv_preload.c getenv_test.c | cjson_opt
	gcc -shared -fPIC getenv_preload.c -ldl -o libgetenv.so
	gcc getenv_test.c -o $@
	# LD_PRELOAD=./libgetenv.so ./cjson_opt --set-env-exec test.json ./$@
	LD_PRELOAD=./libgetenv.so ./cjson_opt --set-env-shell test.json ./$@
