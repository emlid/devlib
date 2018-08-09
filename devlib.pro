TEMPLATE = subdirs

SUBDIRS += \
   devlib \


DEVLIB_INCLUDE_EXAMPLES {
   SUBDIRS += examples
   examples.depends = devlib
}
