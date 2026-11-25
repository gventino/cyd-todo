# docker build -t cyd-todo-server ./server
# docker run -d --name cyd-todo -p 8080:8080 -v cyd-todo-data:/data --env-file cyd-todo.env cyd-todo-server
FROM rust:1 AS build
WORKDIR /src
COPY Cargo.toml Cargo.lock ./
COPY migrations ./migrations
COPY src ./src
RUN cargo build --release --locked

FROM debian:stable-slim
RUN useradd --system --home /data --create-home cyd-todo
COPY --from=build /src/target/release/cyd-todo-server /usr/local/bin/cyd-todo-server
USER cyd-todo
WORKDIR /data
ENV BIND=0.0.0.0:8080 DATABASE_URL=sqlite:///data/cyd-todo.db
EXPOSE 8080
CMD ["cyd-todo-server", "serve"]
