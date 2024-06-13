#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cjson/cJSON.h>

#include "jobs.h"
#include "util.h"

static void output_free(struct output *output);
static int job_output_insert(struct job *j, char *name, char *store_path);
static int job_read_inputdrvs(struct job *job, cJSON *input_drvs);
static int job_read_outputs(struct job *job, cJSON *outputs);

static void output_free(struct output *output)
{
	if (output == NULL)
		return;

	free(output->name);
	free(output->store_path);

	free(output);
}

static int job_output_insert(struct job *j, char *name, char *store_path)
{
	struct output *o;

	o = malloc(sizeof(*o));
	if (o == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	o->name = name;
	o->store_path = store_path;
	LIST_INSERT_HEAD(&j->outputs, o, dlist);

	return 0;
}

static int job_read_inputdrvs(struct job *job, cJSON *input_drvs)
{
	cJSON *output;

	struct job *dep_job = NULL;
	char *drv_path = NULL;
	char *out_name = NULL;
	int ret = 0;

	for (cJSON *array = input_drvs; array != NULL; array = array->next) {
		drv_path = strdup(array->string);
		if (drv_path == NULL) {
			ret = -EPERM;
			goto out_free;
		}

		ret = job_new(&dep_job, NULL, drv_path);
		if (ret < 0) {
			ret = -EPERM;
			goto out_free;
		}

		cJSON_ArrayForEach (output, array) {
			out_name = strdup(output->valuestring);
			if (out_name == NULL) {
				ret = -EPERM;
				goto out_free;
			}

			ret = job_output_insert(dep_job, out_name, NULL);
			if (ret < 0) {
				job_free(dep_job);
				goto out_free;
			}
		}
		LIST_INSERT_HEAD(&job->deps, dep_job, dlist);

		drv_path = NULL;
		out_name = NULL;
		dep_job = NULL;
	}

out_free:
	if (ret < 0) {
		free(drv_path);
		free(out_name);
		job_free(dep_job);
	}

	return ret;
}

static int job_read_outputs(struct job *job, cJSON *outputs)
{
	char *out_name = NULL;
	char *out_path = NULL;
	int ret = 0;

	for (cJSON *i = outputs; i != NULL; i = i->next) {
		out_name = strdup(i->string);
		if (out_name == NULL) {
			ret = -EPERM;
			goto out_free;
		}

		out_path = strdup(i->valuestring);
		if (out_path == NULL) {
			ret = -EPERM;
			goto out_free;
		}

		ret = job_output_insert(job, out_name, out_path);
		if (ret < 0) {
			ret = -EPERM;
			goto out_free;
		}

		out_path = NULL;
		out_name = NULL;
	}

out_free:
	if (ret < 0) {
		free(out_name);
		free(out_path);
	}

	return 0;
}

int job_read(FILE *stream, struct job **job)
{
	cJSON *temp;

	int ret = 0;
	cJSON *root = NULL;
	char *name = NULL;
	char *drv_path = NULL;

	ret = json_streaming_read(stream, &root);
	if (ret < 0 || ret == -EOF)
		return ret;

	temp = cJSON_GetObjectItemCaseSensitive(root, "name");
	if (!cJSON_IsString(temp)) {
		ret = -EPERM;
		goto out_free;
	}
	name = strdup(temp->valuestring);
	if (name == NULL) {
		ret = -EPERM;
		goto out_free;
	}

	temp = cJSON_GetObjectItemCaseSensitive(root, "drvPath");
	if (!cJSON_IsString(temp)) {
		ret = -EPERM;
		goto out_free;
	}
	drv_path = strdup(temp->valuestring);
	if (drv_path == NULL) {
		ret = -EPERM;
		goto out_free;
	}

	ret = job_new(job, name, drv_path);
	if (ret < 0)
		goto out_free;

	temp = cJSON_GetObjectItemCaseSensitive(root, "inputDrvs");
	if (!cJSON_IsObject(temp)) {
		ret = -EPERM;
		goto out_free;
	}
	ret = job_read_inputdrvs(*job, temp->child);
	if (ret < 0)
		goto out_free;

	temp = cJSON_GetObjectItemCaseSensitive(root, "outputs");
	if (!cJSON_IsObject(temp)) {
		ret = -EPERM;
		goto out_free;
	}
	ret = job_read_outputs(*job, temp->child);
	if (ret < 0)
		goto out_free;

out_free:
	cJSON_Delete(root);
	if (ret < 0) {
		print_err("%s", "Invalid JSON");

		free(name);
		free(drv_path);
	}

	return ret;
}

void job_free(struct job *job)
{
	struct job *j;
	struct output *o;

	if (job == NULL)
		return;

	free(job->name);
	free(job->drv_path);

	LIST_FOREACH (o, &job->outputs, dlist)
		output_free(o);

	LIST_FOREACH (j, &job->deps, dlist)
		job_free(j);

	free(job);
}

int job_new(struct job **j, char *name, char *drv_path)
{
	struct job *job;

	job = malloc(sizeof(*job));
	if (job == NULL) {
		print_err("%s", strerror(errno));
		return -errno;
	}

	job->name = name;
	job->drv_path = drv_path;
	LIST_INIT(&job->deps);
	LIST_INIT(&job->outputs);

	*j = job;
	return 0;
}

int jobs_init(FILE **stream)
{
	int ret;

	/* TODO: proproperly handle args */
	char *const args[] = {
		"nix-eval-jobs",
		"--flake",
		"github:sinanmohd/evanix#packages.x86_64-linux",
		NULL,
	};

	/* the package is wrapProgram-ed with nix-eval-jobs  */
	ret = vpopen(stream, "nix-eval-jobs", args);
	return ret;
}
