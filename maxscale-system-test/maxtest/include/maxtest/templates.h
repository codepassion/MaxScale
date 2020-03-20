struct TestDefinition
{
    const char* test_name;
    const char* test_template;
    const char* test_labels;
};

extern const TestDefinition* cnf_templates;

/** The default template to use */
extern const char* default_template;

/** This is the working directory for all tests */
extern const char* test_dir;

