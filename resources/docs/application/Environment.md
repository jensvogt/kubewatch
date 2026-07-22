## Environment

### Variable replacements for a Spring Boot Java application

For a Spring Boot Java application the environment variables defined in the table are replacements for the variables
normally places in the ```application.properties```. Spring Boot has the convention, that ```application.properties```
variables can be overwritten by environment variables, whereas the '.' and '-' are replaced by '_'. Therefore, an
application property in the ```application.properties``` file like:

```bash
spring.datasource.url=jdbc:postgresql://localhost:5432/postgres
```

can be overwritten by an environment variable:

```bash
SPRING_DATASOURCE_URL=jdbc:postgresql://localhost:5432/postgres
```

In order to simplify the environment, you can use the environment variables in this tab to overwrite the variables in
your application property file. This is also working for profiles bases property files, like:
```application-dev.properties```.