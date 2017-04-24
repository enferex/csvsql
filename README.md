csvsql: Turn a CSV file into a sqlite3 databse.
===============================================
csvsql is simple and quick way to take a csv file and query it
as if it were a sqlite3 database.

Notes
-----
* Comments are prefixed with '#'.
* Delimiters are ','. 
* If a comment line exists before the  first line of csv data, that line will be
  parsed. If that comment line looks like a csv description the columns of the
  table will be named accordingly.
* If csvsql cannot guess a good
  column name, then columns will be named 'Cx' where 'x' is a monotonically
  increasing integer from 0 to n.  Where 'n' is the number of columns in a row
  of csv data.

Dependencies
------------
1. GNU readline library, you probably have this.
2. SQLite: https://www.sqlite.org/

Contact
-------
Matt Davis (enferex)

If you want more features or find a bug, feel free to reach out to me
via github: http://github.com/enferex
