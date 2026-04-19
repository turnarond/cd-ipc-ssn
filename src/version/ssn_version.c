/*
 * ssn_version.c - Version information implementation
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ssn_version.h"

const char *ssn_version_get_string(void)
{
    return SSN_VERSION_STRING;
}

int ssn_version_get_major(void)
{
    return SSN_VERSION_MAJOR;
}

int ssn_version_get_minor(void)
{
    return SSN_VERSION_MINOR;
}

int ssn_version_get_patch(void)
{
    return SSN_VERSION_PATCH;
}

bool ssn_version_is_compatible(int major, int minor)
{
    return (major == SSN_VERSION_MAJOR && minor <= SSN_VERSION_MINOR);
}
