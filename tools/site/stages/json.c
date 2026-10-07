/* site_stages: JSON written piece by piece (stages.h). */
#include "stages.h"

/* The separator before a value, and its key inside an object. */
static void json_key(json_t *j, const char *key) {
    if (j->fields++) {
        fputs(", ", j->f);
    }
    if (key) {
        fprintf(j->f, "\"%s\": ", key);
    }
}

json_t json_begin(FILE *f) {
    fputc('{', f);
    return (json_t){f, 0};
}

void json_end(json_t *j) { fputs("}\n", j->f); }

json_t json_object(json_t *j, const char *key) {
    json_key(j, key);
    fputc('{', j->f);
    return (json_t){j->f, 0};
}

void json_close_object(json_t *o) { fputc('}', o->f); }

json_t json_array(json_t *j, const char *key) {
    json_key(j, key);
    fputc('[', j->f);
    return (json_t){j->f, 0};
}

void json_close_array(json_t *a) { fputc(']', a->f); }

void json_null(json_t *j, const char *key) {
    json_key(j, key);
    fputs("null", j->f);
}

void json_int(json_t *j, const char *key, long long v) {
    json_key(j, key);
    fprintf(j->f, "%lld", v);
}

void json_double(json_t *j, const char *key, double v) {
    json_key(j, key);
    fprintf(j->f, "%.6f", v);
}

void json_float(json_t *j, const char *key, float v) {
    json_key(j, key);
    fprintf(j->f, "%.9g", (double)v);
}

void json_string(json_t *j, const char *key, const char *v) {
    json_key(j, key);
    fprintf(j->f, "\"%s\"", v);
}

void json_hex64(json_t *j, const char *key, uint64_t v) {
    json_key(j, key);
    fprintf(j->f, "\"%016llx\"", (unsigned long long)v);
}

void json_hexbytes(json_t *j, const char *key, const uint8_t *v, int n) {
    json_key(j, key);
    fputc('"', j->f);
    for (int i = 0; i < n; i++) {
        fprintf(j->f, "%02x", v[i]);
    }
    fputc('"', j->f);
}

void json_u8s(json_t *j, const char *key, const uint8_t *v, int n) {
    json_t a = json_array(j, key);
    for (int i = 0; i < n; i++) {
        json_int(&a, NULL, v[i]);
    }
    json_close_array(&a);
}

void json_u64s(json_t *j, const char *key, const uint64_t *v, int n) {
    json_t a = json_array(j, key);
    for (int i = 0; i < n; i++) {
        json_key(&a, NULL);
        fprintf(a.f, "%llu", (unsigned long long)v[i]);
    }
    json_close_array(&a);
}

void json_floats(json_t *j, const char *key, const float *v, int n) {
    json_t a = json_array(j, key);
    for (int i = 0; i < n; i++) {
        json_float(&a, NULL, v[i]);
    }
    json_close_array(&a);
}

void json_doubles(json_t *j, const char *key, const double *v, int n) {
    json_t a = json_array(j, key);
    for (int i = 0; i < n; i++) {
        json_key(&a, NULL);
        fprintf(a.f, "%.9g", v[i]);
    }
    json_close_array(&a);
}

void json_doubles_fixed(json_t *j, const char *key, const double *v, int n, int decimals) {
    json_t a = json_array(j, key);
    for (int i = 0; i < n; i++) {
        json_key(&a, NULL);
        fprintf(a.f, "%.*f", decimals, v[i]);
    }
    json_close_array(&a);
}

void json_bits_msb(json_t *j, const char *key, uint64_t bits, int n) {
    json_t a = json_array(j, key);
    for (int i = 0; i < n; i++) {
        json_int(&a, NULL, (int)((bits >> (n - 1 - i)) & 1));
    }
    json_close_array(&a);
}

void json_bits_lsb(json_t *j, const char *key, uint64_t bits, int n) {
    json_t a = json_array(j, key);
    for (int i = 0; i < n; i++) {
        json_int(&a, NULL, (int)((bits >> i) & 1));
    }
    json_close_array(&a);
}
