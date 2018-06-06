TEMPLATE = subdirs

SUBDIRS += \
   devlib \


!EXCLUDE_EXAMPLES_BUILD {
   SUBDIRS += examples
   examples.depends = devlib
}
