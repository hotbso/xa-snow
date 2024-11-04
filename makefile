mkfile_path := $(abspath $(lastword $(MAKEFILE_LIST)))
current_dir := $(notdir $(patsubst %/,%,$(dir $(mkfile_path))))


clean:
	rm -r dist || true || rm ~/X-Plane\ 12/Resources/plugins/XA-snow/mac.xpl
mac:
	GOOS=darwin \
	GOARCH=arm64 \
	CGO_ENABLED=1 \
	CGO_CFLAGS="-DAPL=1 -DIBM=0 -DLIN=0 -O0 -g" \
	CGO_LDFLAGS="-F/System/Library/Frameworks/ -F${CURDIR}/Libraries/Mac -framework XPLM" \
	go build -buildmode c-shared -o build/XA-snow/mac_arm.xpl main.go
	GOOS=darwin \
	GOARCH=amd64 \
	CGO_ENABLED=1 \
	CGO_CFLAGS="-DAPL=1 -DIBM=0 -DLIN=0 -O0 -g" \
	CGO_LDFLAGS="-F/System/Library/Frameworks/ -F${CURDIR}/Libraries/Mac -framework XPLM" \
	go build -buildmode c-shared -o build/XA-snow/mac_amd.xpl main.go
	lipo build/XA-snow/mac_arm.xpl build/XA-snow/mac_amd.xpl -create -output build/XA-snow/mac.xpl
dev:
	GOOS=darwin \
	GOARCH=arm64 \
	CGO_ENABLED=1 \
	CGO_CFLAGS="-DAPL=1 -DIBM=0 -DLIN=0 -O0 -g" \
	CGO_LDFLAGS="-F/System/Library/Frameworks/ -F${CURDIR}/Libraries/Mac -framework XPLM" \
	go build -buildmode c-shared -o ~/X-Plane\ 12/Resources/plugins/XA-snow/mac.xpl main.go
win:
	CGO_CFLAGS="-DIBM=1 -static -O0 -g" \
	CGO_LDFLAGS="-L${CURDIR}/Libraries/Win -lXPLM_64 -static-libgcc -static-libstdc++ -Wl,--exclude-libs,ALL" \
	GOOS=windows \
	GOARCH=amd64 \
	CGO_ENABLED=1 \
	CC=x86_64-w64-mingw32-gcc \
	CXX=x86_64-w64-mingw32-g++ \
	go build --buildmode c-shared -o build/XA-snow/win.xpl main.go
msys2:
	CGO_CFLAGS="-DIBM=1 -static -O0 -g" \
	CGO_LDFLAGS="-L${CURDIR}/Libraries/Win -lXPLM_64 -static-libgcc -static-libstdc++" \
	GOOS=windows \
	GOARCH=amd64 \
	CGO_ENABLED=1 \
	CC=gcc \
	CXX=g++ \
	go build --buildmode c-shared -o build/XA-snow/win.xpl main.go
	cp -p build/XA-snow/win.xpl /E/X-Plane-12/Resources/plugins/XA-snow/.
lin:
	GOOS=linux \
	GOARCH=amd64 \
	CGO_ENABLED=1 \
	CC=/usr/local/bin/x86_64-linux-musl-cc \
	CGO_CFLAGS="-DLIN=1 -O0 -g" \
	CGO_LDFLAGS="-shared -rdynamic -nodefaultlibs -undefined_warning" \
	go build -buildmode c-shared -o build/XA-snow/lin.xpl main.go

all: mac win lin
mac-test:
	GOOS=darwin \
	GOARCH=arm64 \
	CGO_ENABLED=1 \
	CGO_CFLAGS="-DAPL=1 -DIBM=0 -DLIN=0 -O0 -g" \
	CGO_LDFLAGS="-F/System/Library/Frameworks/ -F${CURDIR}/Libraries/Mac -framework XPLM" \
	go test -race -coverprofile=coverage.txt -covermode=atomic ./... -v
