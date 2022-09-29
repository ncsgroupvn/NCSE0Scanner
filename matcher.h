#pragma once

int wc_matcher(const char* text, int len, const char* pt, int pt_len, int nb_wildcard);
int wc_matcher_i(const char* text, int len, const char* pt, int pt_len, int nb_wildcard);