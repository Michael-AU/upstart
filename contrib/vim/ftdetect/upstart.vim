" Treat all /etc/init/*.conf files as upstart job files
au BufNewFile,BufRead /etc/init/*.conf	set filetype=upstart
au BufNewFile,BufRead *.upstart	set filetype=upstart
