# Postgres Extensions (PGXS)

- Written in C.
- Includes PGXS CMake build environment.
- New extensions can be added easily.

*(C) 2022 KetteQ*

# Build Requirements

This extension should be compiled in a Linux host. Building in macOS or Windows
hosts is not supported.

## Ubuntu/Debian Linux

1. Install development dependencies:

    ```bash
    sudo apt install build-essential pkg-config postgresql-server-dev-14 libgtk2.0-dev libpq-dev libpam-dev libxslt-dev liblz4-dev libreadline-dev libkrb5-dev cmake
    ```

2. Clone this repo.
3. `cd` to newly cloned repo.
4. Clone `Google Test` inside it:

    ```bash
    git clone https://github.com/google/googletest 
    ```
5. If not using an IDE (such as CLion from Jetbrains) create `build` folders:

    ```bash
    mkdir build 
    ```
6. CD to the newly created build folder and generate Makefiles using `cmake`
    
    ```bash
    cd build
    cmake .. 
    ```
7. Run `make` to create the extension shared object.

    ```bash
    make 
    ```
   
* Other build helpers such as `ninja` can be used as well.

# Extensions And Features

| Extension Name                       | Create Extension Name | Description                                                          |
|--------------------------------------|-----------------------|----------------------------------------------------------------------|
| In-Memory Calendar Extension (IMCX)  | `kq_imcx`             | Loads slices into memory and provides calendar calculation functions |

## In-Memory Calendar Extension (IMCX)

- Uses GHashTable to store slices cache in memory.
- Uses the PostgreSQL Server's memory.
- Provides very fast calendar calculation functions.

# Installation

## In-Memory Calendar Extension (IMCX)

Required files for installation:

| File                | Description              | Manual Install Path                                   |
|---------------------|--------------------------|-------------------------------------------------------|
| `kq_imcx--0.1.sql`* | Extension Mapping File   | `/usr/share/postgresql/14/extension/kq_imcx--0.1.sql` |
| `kq_imcx.control`   | Extension Control File   | `/usr/share/postgresql/14/extension/kq_imcx.control`  |
| `kq_imcx.so`        | Extension Shared Library | `/usr/lib/postgresql/14/lib/kq_imcx.so`               |

* The Extension Mapping File should be copied from the source folder.

Install as normal Postgres Extension, with release binaries built, copy
"PostgreSQL Extension Mapping File" `kq_imcx--0.1.sql`, 
"Extension Control File" `kq_imcx.control` and "Extension Shared Library" 
`kq_imcx.so` to the target PostgreSQL Extension Folder.

As an alternate method, if you want to install in the same computer that
has built the extension use the automatic installation option:

```bash
sudo make install
```

The installation script will automatically find the correct path to install the
extension.

After installation restart the database instance.

```bash
sudo systemctl restart postgresql
```

# Removal / Uninstall

Just delete all three extension files from the PostgreSQL's extensions folder. Sudo/Root access may be required.

After manual deletion of files, restart PostgreSQL Server.

# Usage

## In-Memory Calendar Extension (IMCX)

Enable the extension using the `CREATE EXTENSION IF NOT EXISTS kq_imcx` command. This command can
be emitted using `psql` using a **superuser account** (like the `postgres` account). 

To use the extension with a non-superuser account, you should connect first using the superuser account, 
switch to the target client database and from there issue the create extension command. After that, the 
extension will be available to any user that has access to that DB. 

Is not recommended to give superuser powers to an account just to enable the extension.

After the extension is enabled the following functions will be available
from the SQL-query interface:

| Function                                                                   | Description                                                               |
|----------------------------------------------------------------------------|---------------------------------------------------------------------------|
| kq_imcx_info()                                                             | Returns information about the extension as records.                       |
| kq_imcx_invalidate()                                                       | Invalidates the loaded cache.                                             |
| kq_imcx_report(`showEntries int`,`showPageMap int `,`showSliceNames int`)  | List the cached calendars.                                                |   
| kq_imcx_add_days(`input date`, `interval int`, `slicetype-id int`)         | Calculate the next or previous date using the calendar ID.                |
| kq_imcx_add_days_name(`input date`, `interval int`, `slicetype-name text`) | Same as the previous function but uses the calendar NAMEs instead of IDs. |


# Architecture

Postgres' extensions are handled by a "bridge" (or main) C file with functions that must be mapped into the 
"extension mapping" SQL file that will make these C functions available from the SQL query interface.

## Example (In-Memory Calendar Extension)

| File                           | Description                       |
|--------------------------------|-----------------------------------|
| src/imcx/pgxs/kq_imcx.c        | Extension C Bridge (Main)         |
| src/imcx/pgxs/kq_imcx--0.1.sql | PostgreSQL Extension Mapping File |
| src/imcx/src/                  | Extension C Source Files          |

