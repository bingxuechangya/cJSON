#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <getopt.h>
#include <math.h>
#include <unistd.h>
#include <libgen.h>

#include "cJSON.h"

#define CONF_MAX_SZ 4096
static int g_value_in_subnode = 0;

cJSON *cJSON_ParseFile(const char *file)
{
	cJSON *json;
	FILE *fp;
	char *buf;
	unsigned long len;
	fp = fopen(file, "rb");
	if (fp == NULL) return NULL;

	fseek(fp, 0, SEEK_END);
	len = (unsigned long)ftell(fp);
	if (len >= 8 * 1024 * 1024) {
		fclose(fp);
		return NULL;
	}
	fseek(fp, 0, SEEK_SET);
	buf = (char *)malloc(len);
	if (buf == NULL) {
		fclose(fp);
		return NULL;
	}
	fread(buf, 1, len, fp);
	fclose(fp);
	json = cJSON_Parse(buf);
	free(buf);
	return json;
}

int cJSON_WriteFile(const char *file,cJSON *json)
{

	FILE *fp;
	char *buf;
	fp = fopen(file, "wb");
	if (fp == NULL) return -1;
	buf = cJSON_PrintBuffered(json, 2048, 1);
	if (buf) {
		// cJSON_Minify(buf);
		fwrite(buf, 1, strlen(buf), fp);
		free(buf);
	}
	fclose(fp);
	return 0;
}

// 保存json的末端值节点(如{"type":"int","value":12})的"value"到conf里，不受层数、数组限制，目前支持value的类型为string、int、float。
// conf 为 NULL 时就创建保存到 conf 里，不为 NULL 时就从 conf 里面加载到 object
// cJSON *cJSON_TO_Conf(cJSON *object, char *keychain, char *conf)
// conf: 输出的conf内容
// conf_len: 限定conf最大长度
// keychain: json的key拼接成 A.B.C 形式
// sep: json的key拼接时用的连接符，这里为'.'
// root_flag: keychain第一级前面是否加sep
// object: 输入的 json 对象
// func: 找到值时执行的处理函数，可参考下面的“处理函数N”
static int cJSON_Parse_Value_Internal(char *conf, size_t conf_len, char *keychain, char sep, int root_flag, cJSON *object,
								  int (*func)(const char *type, char *conf, size_t conf_len, char *keychain, cJSON *c))
{
	cJSON *c = object->child;
	char *newkey_start;
	size_t newkey_len;
	int len = 0;

	if (g_value_in_subnode) {
		if (c == NULL) return 0;
	   /** 注:
		* 把值节点这段处理稍微调整一下就能变成把旧json文件里的所有值更新到新json的默认文件里，
		* 但前提是指定json文件里只有适配参数。下面这段默认适配参数都保存在值节点的"value"里，
		* 其它字段可以用来配置页面，只要更新json就能改变页面的布局，使用起来更加灵活。
		*  */
		// 取到该节点的值属性，如string,int,float，如果不是值节点，那么type为NULL。
		cJSON *type_item = cJSON_GetObjectItem(object, "type");
		if (type_item) { // 发现object节点是值节点(形如: {"type": "string", "value": "abc"})
			while(c && strcmp(c->string, "value")) {
				// 跳过值节点的所有其它，只需要取到值
				c = c->next;
			}
			if (!c) return len;

			char *type = cJSON_GetStringValue(type_item);
			// 用 func() 来操作值节点
			return func(type, conf, conf_len, keychain, c);
		}
	} else {
		if (c == NULL) {
			return func(NULL, conf, conf_len, keychain, object);
		}
	}

	// 处理数组，套用递归
	if (((object->type) & 0xFF) == cJSON_Array) {
		int index = 0;
		char index_str[4];
		while(c) {
			newkey_start = keychain + strlen(keychain);
			snprintf(index_str, sizeof(index_str), "%d", index++);
			newkey_len = strlen(index_str);
			sprintf(keychain, "%s%s", keychain, index_str);
			// 对子节点递归执行
			len += cJSON_Parse_Value_Internal(conf+len, conf_len-len, keychain, sep, root_flag, c, func);
			memset(newkey_start, 0, newkey_len);
			c = c->next;
		}
		return len;
	}

	// 处理keychain，套用递归
	while (c) {
		// 跳过无意义的节点
		if (c->string == NULL) {
			goto next;
		}

		// 延展keychain。
		newkey_start = keychain + strlen(keychain);
		if ((root_flag) && (keychain[0] == 0)) {
			// 遍历节点过程中，访问第一个节点之前把keychain的根(如:item.sub.A中的item)准备好
			strcpy(keychain, c->string);
			newkey_len = strlen(c->string);
		} else {
			// 遍历根的下级节点的过程中，访问第一个节点之前把keychain(如:item.sub.A)准备好
			sprintf(keychain, "%s%c%s", keychain, sep, c->string);
			newkey_len = strlen(c->string) + 1;
		}

		// 对子节点递归执行
		len += cJSON_Parse_Value_Internal(conf+len, conf_len-len, keychain, sep, root_flag, c, func);

		memset(newkey_start, 0, newkey_len);

	next:
		c = c->next;
	}
	return len;
}

static int cJSON_Parse_Value(char *conf, size_t conf_len, cJSON *object,
	  int (*func)(const char *type, char *conf, size_t conf_len, char *keychain, cJSON *c))
{
	int ret;
	char *keychain = calloc(1, 256);
	ret = cJSON_Parse_Value_Internal(conf, conf_len, keychain, '.', 0, object, func);
	free(keychain);
	return ret;
}

// 处理函数1: 把压平后的值 keychain=value 粘连成 conf 文件
static int cJSON_Parse_Value_Conf_Join(const char *type, char *conf, size_t conf_len, char *keychain, cJSON *c)
{
	int len=0;
	if (type == NULL) {
		char *value_str;
		double value_double;
		value_double = cJSON_GetNumberValue(c);
		if (isnan(value_double) || isinf(value_double)) {
			value_str = cJSON_GetStringValue(c);
			len = snprintf(conf+len, conf_len-len, "%s=\"%s\"\n", keychain, (value_str==NULL)?"":value_str);
		} else if (value_double == (long)value_double) {
			len = snprintf(conf+len, conf_len-len, "%s=%ld\n", keychain, (long)value_double);
		} else {
			len = snprintf(conf+len, conf_len-len, "%s=%lf\n", keychain, value_double);
		}
	} else if (!strcmp(type, "string")) {
		len = snprintf(conf+len, conf_len-len, "%s=\"%s\"\n", keychain, cJSON_GetStringValue(c));
	} else if (!strcmp(type, "int")) {
		len = snprintf(conf+len, conf_len-len, "%s=%ld\n", keychain, (long)cJSON_GetNumberValue(c));
	} else if (!strcmp(type, "float")) {
		len = snprintf(conf+len, conf_len-len, "%s=%lf\n", keychain, cJSON_GetNumberValue(c));
	}
	return len;
}

static int get_conf_key_value(const char *conf, size_t conf_len, const char *key, char *value, size_t value_len)
{
	const char *ptr = conf;
	const char *end;
	size_t len = strlen(key);
	while(strncmp(ptr, key, len) && ((size_t)(ptr-conf) < conf_len)) {
		ptr = index(ptr, '\n');
		if (ptr == NULL) return -1;
		ptr++;
	}
	ptr = index(ptr, '=');
	if (ptr == NULL) return -1;
	ptr++;
	// strncpy(value, ptr, value_len);
	// sscanf(ptr, "%*s", value_len, value);
	len = strcspn(ptr, "\r\n");
	if (*ptr == '"') {
		ptr++;
		end = ptr+len;
		while(*end != '"') end--;
		len = end-ptr;
	} else if (*ptr == '\'') {
		ptr++;
		end = ptr+len;
		while(*end != '\'') end--;
		len = end-ptr;
	}
	len = value_len > len ? len : value_len - 1;
	strncpy(value, ptr, len);
	value[len] = 0;
	return 0;
}

// 处理函数2: 用conf里的值更新json里的值
static int cJSON_Parse_Value_Update(const char *type, char *conf, size_t conf_len, char *keychain, cJSON *c)
{
	char *value = NULL;
	value = (char *)calloc(1, 256);
	if (get_conf_key_value(conf, conf_len, keychain, value, 256) < 0) {
		free(value);
		return 0;
	}
	if (type == NULL) {
		double value_double;
		value_double = cJSON_GetNumberValue(c);
		if (isnan(value_double) || isinf(value_double)) {
			cJSON_SetValuestring(c, value);
		} else if (value_double == (long)value_double) {
			cJSON_SetNumberHelper(c, strtod(value,NULL));
		} else {
			cJSON_SetNumberHelper(c, strtof(value,NULL));
		}
	} else if (!strcmp(type, "string")) {
		cJSON_SetValuestring(c, value);
	} else if (!strcmp(type, "int")) {
		cJSON_SetNumberHelper(c, strtod(value,NULL));
	} else if (!strcmp(type, "float")) {
		cJSON_SetNumberHelper(c, strtof(value,NULL));
	}
	free(value);
	return 0;
}

// 处理函数3: 直接打印压平后的值
static int cJSON_Parse_Value_Print(const char *type, char *conf __attribute__((unused)),
								   size_t conf_len __attribute__((unused)), char *keychain,
								   cJSON *c)
{

	if (type == NULL) {
		char *value_str;
		double value_double;
		value_double = cJSON_GetNumberValue(c);
		if (isnan(value_double) || isinf(value_double)) {
			value_str = cJSON_GetStringValue(c);
			printf("%s=\"%s\"\n", keychain, (value_str==NULL)?"":value_str);
		} else if (value_double == (long)value_double) {
			printf("%s=%ld\n", keychain, (long)value_double);
		} else {
			printf("%s=%lf\n", keychain, value_double);
		}
	} else if (!strcmp(type, "string")) {
		printf("%s=\"%s\"\n", keychain, cJSON_GetStringValue(c));
	} else if (!strcmp(type, "int")) {
		printf("%s=%ld\n", keychain, (long)cJSON_GetNumberValue(c));
	} else if (!strcmp(type, "float")) {
		printf("%s=%lf\n", keychain, cJSON_GetNumberValue(c));
	}
	return 0;
}

// 处理函数4: 把压平后的值设为环境变量
static int cJSON_Parse_Value_Set_Env(const char *type, char *conf __attribute__((unused)),
									 size_t conf_len __attribute__((unused)), char *keychain,
									 cJSON *c)
{
	char value[32];

	if (type == NULL) {
		char *value_str;
		double value_double;
		value_double = cJSON_GetNumberValue(c);
		if (isnan(value_double) || isinf(value_double)) {
			value_str = cJSON_GetStringValue(c);
			setenv(keychain, (value_str==NULL)?"":value_str, 1);
		} else if (value_double == (long)value_double) {
			snprintf(value, sizeof(value), "%ld", (long)cJSON_GetNumberValue(c));
			setenv(keychain, value, 1);
		} else {
			snprintf(value, sizeof(value), "%lf", cJSON_GetNumberValue(c));
			setenv(keychain, value, 1);
		}
	} else if (!strcmp(type, "string")) {
		setenv(keychain, cJSON_GetStringValue(c), 1);
	} else if (!strcmp(type, "int")) {
		snprintf(value, sizeof(value), "%ld", (long)cJSON_GetNumberValue(c));
		setenv(keychain, value, 1);
	} else if (!strcmp(type, "float")) {
		snprintf(value, sizeof(value), "%lf", cJSON_GetNumberValue(c));
		setenv(keychain, value, 1);
	}
	return 0;
}

char *cJSON_TO_Conf(cJSON *object);
char *cJSON_TO_Conf(cJSON *object)
{
	char *conf = calloc(1, CONF_MAX_SZ);
	cJSON_Parse_Value(conf, CONF_MAX_SZ, object, cJSON_Parse_Value_Conf_Join);
	return conf;
}

#if 0
int main(int argc, char *argv[])
{
	cJSON *json_obj;
	char *conf;
	json_obj = cJSON_ParseFile("webcfg.json");
	if (json_obj == NULL) {
		printf("err\n");
		return 0;
	}

	conf = cJSON_TO_Conf(json_obj);
	cJSON_Delete(json_obj);
	puts(conf);
	free(conf);
	return 0;
}
#endif

int cJSON_Update_By_Conf(cJSON *object, const char *conf);
int cJSON_Update_By_Conf(cJSON *object, const char *conf)
{
	return cJSON_Parse_Value((char*)conf, CONF_MAX_SZ, object, cJSON_Parse_Value_Update);
}

int cJSON_Env_Exec(cJSON *object, char *prog);
int cJSON_Env_Exec(cJSON *object, char *prog)
{
	int ret;
	ret = cJSON_Parse_Value(NULL, 0, object, cJSON_Parse_Value_Set_Env);
	if (ret < 0) {
		fprintf(stderr, "Failed to set env!\n");
		return ret;
	}
	execl(prog, basename(prog), (char *)NULL);
	return ret;
}

int cJSON_Set_Env_Exec(int args_num, char **args)
{
	int ret;
	cJSON *obj;
	char *json_file;
	char *prog;
	char *prog_argv[args_num+1];
	char prog_base_name[64];

	if (args_num < 2) {
		return -1;
	}

	json_file = args[0];
	prog = args[1];

	obj = cJSON_ParseFile(json_file);
	if (obj == NULL) return -1;

	ret = cJSON_Parse_Value(NULL, 0, obj, cJSON_Parse_Value_Set_Env);
	if (ret < 0) {
		fprintf(stderr, "Failed to set env!\n");
		cJSON_Delete(obj);
		return ret;
	}
	cJSON_Delete(obj);

	memcpy(&prog_argv[1], &args[1], args_num*sizeof(prog_argv[0]));
	prog_argv[0] = prog_argv[1];

	snprintf(prog_base_name, sizeof(prog_base_name), "%s", basename(prog));
	prog_argv[1] = prog_base_name;

	prog_argv[args_num+1] = NULL;

	execv(prog, prog_argv);
}

#if 0
int main(int argc, char *argv[])
{
	const char *conf = ".rsu.rsu_ip=192.168.0.224\n"
		".rsu.rsu_netmask=255.255.255.0\n"
		".rsu.rsu_gateway=192.168.0.1\n"
		".rsu._rsu_dns=255.255.255.0\n"
		".rsu.rsu_power=73709551612\n"
		".rsu.enable_test=on\n"
		".detect.obu_detect_interval=25\n"
		".detect.obu_record_interval=120\n"
		".detect.bluetooth_record_interval=20\n"
		".detect.wifi_record_interval=20\n"
		".detect._beacon_id=0xb7,0,0,1\n"
		".remote.enable_ntp=on\n"
		".remote.psql_server=192.168.1.220:5432\n"
		".remote.ntp_server=192.168.1.220\n";
	cJSON *json_obj;
	json_obj = cJSON_ParseFile("webcfg.json");
	cJSON_Update_By_Conf(json_obj, conf);
	puts(cJSON_Print(json_obj));

	cJSON_Env_Exec(json_obj, argv[1]);
	return 0;
}
#endif

int cJSON_Print_Only(char *json_file)
{
	cJSON *obj;
	obj = cJSON_ParseFile(json_file);
	if (obj == NULL) return -1;
	cJSON_Parse_Value(NULL, 0, obj, cJSON_Parse_Value_Print);
	// cJSON_Flatten(NULL, 0, obj, cJSON_Parse_Value_Print);
	cJSON_Delete(obj);
	return 0;
}

// 可以把旧的已赋值的配置文件的值提取出来，并按照新的配置文件格式覆盖掉配置文件里的默认值。
// 输入文件: new_default_json_file, old_json_file
// 输出文件: new_json_file
int cJSON_Update_By_JSON(char *old_json_file, char *new_default_json_file, char *new_json_file)
{
	char fname[256];
	cJSON *old_obj, *new_obj;
	new_obj = cJSON_ParseFile(new_default_json_file);
	if (new_obj == NULL) {
		fprintf(stderr, "Cannot parse %s\n", new_default_json_file);
		return -1;
	}
	if (new_json_file == NULL) {
		snprintf(fname, sizeof(fname), "%s.orig", old_json_file);
		rename(old_json_file, fname);
		new_json_file = old_json_file;
		old_json_file = fname;
	}
	// puts(cJSON_Print(new_obj));
	old_obj = cJSON_ParseFile(old_json_file);
	if (old_obj == NULL) {
		cJSON_WriteFile(new_json_file, new_obj);
		cJSON_Delete(new_obj);
		return 0;
	}
	// puts(cJSON_Print(old_obj));

	char *conf = calloc(1, CONF_MAX_SZ);

	// 把旧的old_obj json压平导出到conf
	cJSON_Parse_Value(conf, CONF_MAX_SZ, old_obj, cJSON_Parse_Value_Conf_Join);

	// 用conf更新new_obj json
	cJSON_Parse_Value(conf, CONF_MAX_SZ, new_obj, cJSON_Parse_Value_Update);

	// 打印更新后的 new_obj json
	cJSON_Parse_Value(conf, CONF_MAX_SZ, new_obj, cJSON_Parse_Value_Print);
	cJSON_WriteFile(new_json_file, new_obj);

	free(conf);
	cJSON_Delete(new_obj);
	cJSON_Delete(old_obj);
	return 0;
}

void Usage()
{
	if (!strcmp(getenv("LANG"), "zh_CN.UTF-8")) {
	printf(
"用法: cjson_opt [-FLAGS] [--OPTIONS] ARGS...\n"
"  可以很方便地 压平/更新/加载 json 配置。\n"
"  Flags:\n"
"    -s        表示值是存放在子节点中的，如: {\"type\":\"int\",\"value\":123}。\n"
"              若不带此参数则表示所有末梢节点都有值。"
"  Options:\n"
"    --print-only FILE.json    打印FILE.json压平后的内容\n"
"    --update-by-json OLD.json NEW_DEFAULT.json new.json\n"
"              综合OLD.json的值和NEW_DEFAULT.json的格式，得出new.json\n"
"    --update-by-json OLD.json NEW_DEFAULT.json\n"
"              备份OLD.json为OLD.json.orig，综合OLD.json.orig的值\n"
"              和NEW_DEFAULT.json的格式，得出OLD.json\n"
"    --set-env-exec FILE.json PROGRAM [ARGS...]\n"
"              把FILE.json压平后的内容加载成为环境变量，\n"
"              然后带参数ARGS...运行 PROGRAM。\n"
		   );
		return;
	}
	printf(
"Usage: cjson_opt [-FLAGS] [--OPTIONS] ARGS...\n"
"  Useful to flatten/update/load json config.\n"
"  Flags:\n"
"    -s        indicate values stored in subnode such as {\"type\":\"int\",\"value\":123}.\n"
"              if no such flag, it indicates that all the end nodes has a value."
"  Options:\n"
"    --print-only FILE.json    print flattened content of json file.\n"
"    --update-by-json OLD.json NEW_DEFAULT.json new.json\n"
"              composite the value of OLD.json and the format of NEW_DEFAULT.json into new.json.\n"
"    --update-by-json OLD.json NEW_DEFAULT.json\n"
"              backup OLD.json to OLD.json.orig, composite the value of OLD.json\n"
"              and the format of NEW_DEFAULT.json into OLD.json.\n"
"    --set-env-exec FILE.json PROGRAM [ARGS...]\n"
"              load flattened content of FILE.json as environment variables\n"
"              then run PROGRAM with ARGS.\n"
		   );
}

int main(int argc, char *argv[])
{
	int flag = -1;
	int opt_index = 0;
	int i;
	enum OPT {
		PRINT_ONLY,
		UPDATE_BY_CONF,
		UPDATE_BY_JSON,
		SET_ENV_EXEC,
	} opt_flag;
	const struct option long_options[] = {
		{.name = "print-only", .has_arg = no_argument, .flag = &flag, .val = PRINT_ONLY},
		{.name = "update-by-conf", .has_arg = no_argument, .flag = &flag, .val = UPDATE_BY_CONF},
		{.name = "update-by-json", .has_arg = no_argument, .flag = &flag, .val = UPDATE_BY_JSON},
		{.name = "set-env-exec", .has_arg = no_argument, .flag = &flag, .val = SET_ENV_EXEC},
		{.name = "value-in-submode", .has_arg = no_argument, .flag = 0, .val = 's'},
		{},
	};
	while(1) {
		// getopt 或 getopt_long 会重排 argv 的顺序
		int opt = getopt_long(argc, argv, "sh", long_options, &opt_index);
		if (opt == -1) break;
		if (opt == 's') {
			g_value_in_subnode = 1;
			continue;
		}
		if (opt != 0) {
			Usage();
			return -1;
		}
	}
	// printf("%d\n", optind);
	// for (i = optind; i < argc; ++i) {
	//     printf("%s\t", argv[i]);
	// }
    //
	int args_num = argc-optind;
	char **args = &argv[optind];
	// printf("\n");
	// for (i = 0; i < args_num; ++i) {
	//     printf("%s\t", args[i]);
	// }
	switch (flag) {
	case PRINT_ONLY:
		if (args_num < 1) {
			return -1;
		}
		return cJSON_Print_Only(args[0]);
	case UPDATE_BY_CONF:
		return 0;
	case UPDATE_BY_JSON:
		if (args_num < 2) {
			return -1;
		}
		if (args_num == 2) {
			return cJSON_Update_By_JSON(args[0], args[1], NULL);
		}
		return cJSON_Update_By_JSON(args[0], args[1], args[2]);
	case SET_ENV_EXEC:
		if (args_num < 2) {
			return -1;
		}
		cJSON_Set_Env_Exec(args_num, args);
		break;
	default:
		Usage();
		break;
	}
	return 0;
}
