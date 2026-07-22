## Dockerfile

### Variable replacements

In case you need environment variables in your Dockerfile, the variables substitution feature will use the
```$$ENV$$``` to replace the environment variables in the Docker file with the environment variables defined in your
application. All replacements should have the syntax: ```$$<name>$$```.

If our application Dockerfile has the following layout

```
FROM amazoncorretto:25-alpine-jdk

$$ENV$$
            
WORKDIR /app
COPY $$ARCHIVE$$ app.jar

EXPOSE $$PORT$$

ENTRYPOINT ["java", "-jar", "app.jar"]
```

```$$ENV$$``` will be replaces with your environment variables defined in your application entity. The final
Dockerfile, which will be used to create the docker image, will have the following layout:

```bash
FROM amazoncorretto:25-alpine-jdk

AWS_REGION "eu-central-1"
AWS_ACCESS_KEY "none"

WORKDIR /app
COPY java-tool-1.1.0.jar app.jar

EXPOSE 8080:8080

ENTRYPOINT ["java", "-jar", "app.jar"]
```

The following variables are supported:

- ```$$ENV$$```: replaced by applications environment variables
- ```$$PORT$$```: replaced by \<publicPort\>:\<privatePort\>
- ```$$ARCHIVE$$```: replaces by the application archive

### Java application

Java application are deployed usually via an alpine Java image. A typical Java application docker file should look like:

```bash
FROM alpine/java:21-jdk

$$ENV$$

WORKDIR /app
COPY $$ARCHIVE$$ app.jar

EXPOSE $$PORT$$
ENTRYPOINT ["java", "-jar", "app.jar"]
```

First the parent image is loaded, then the environment variables are included. The JAR file is copied to the ```/app```
folder. Afterward, the ports a defined and the application JAR file is started.

### Nodes application

Nodes applications will need to have a nginx server, in which the application will be deployed. Therefore, you need to
provide a nginx.conf configuration file. The archive should have the following directory tree:

```bash
nginx.conf
dist/index.html
dist/....
```

The ```dist``` directory should contain your nodes application code. The nginx.conf will have the Nginx configuration.

A typical nodes application would need to have Dockerfile layout like:

```bash
FROM nginx:alpine

ADD $$ARCHIVE$$ /tmp
RUN cp /tmp/nginx.conf /etc/nginx/conf.d/default.conf
RUN cp -R /tmp/dist/* /usr/share/nginx/html

EXPOSE $$PORT$$

CMD ["nginx", "-g", "daemon off;"]
```

Nodes applications using [nginx](https://nginx.org/) as underlying web server. Therefore, a nginx configuration must be
provided. 